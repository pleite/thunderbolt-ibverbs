// SPDX-License-Identifier: MIT
/*
 * rc_send_churn - RC SEND/RECV QP lifecycle probe.
 *
 * This stresses the small-RPC pattern used by applications that create many
 * short-lived RC QPs.  It can also force recv-credit to arrive before the peer
 * has completed RTR by delaying one side's modify_qp after metadata exchange.
 */

#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAGIC 0x54425343u
#define DEFAULT_PAYLOAD 64u

struct opts {
	const char *role;
	const char *dev;
	const char *connect_host;
	int tcp_port;
	int ib_port;
	int gid_index;
	int iters;
	int timeout_ms;
	int client_rtr_delay_us;
	int server_rtr_delay_us;
	int prepost_before_rtr;
	int verbose;
	uint32_t payload;
};

struct wire_info {
	uint32_t magic;
	uint32_t iter;
	uint32_t qpn;
	uint32_t psn;
	uint32_t lid;
	uint8_t gid[16];
};

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_us(int usec)
{
	struct timespec ts;

	if (usec <= 0)
		return;
	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (long)(usec % 1000000) * 1000;
	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static int send_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while (len) {
		ssize_t n = send(fd, p, len, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
	char *p = buf;

	while (len) {
		ssize_t n = recv(fd, p, len, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!n)
			return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int tcp_listen(int port)
{
	struct sockaddr_in addr = {};
	int fd;
	int one = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
	    listen(fd, 1)) {
		close(fd);
		return -1;
	}
	return fd;
}

static int tcp_connect(const char *host, int port)
{
	struct addrinfo hints = {};
	struct addrinfo *res = NULL;
	char port_s[16];
	int fd = -1;

	snprintf(port_s, sizeof(port_s), "%d", port);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port_s, &hints, &res))
		return -1;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		if (!connect(fd, ai->ai_addr, ai->ai_addrlen))
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s --role server|client --dev DEV [--connect HOST]\n"
		"          [--tcp-port N] [--gid-index N] [--ib-port N]\n"
		"          [--iters N] [--payload BYTES] [--timeout-ms N]\n"
		"          [--client-rtr-delay-us N] [--server-rtr-delay-us N]\n"
		"          [--post-after-rtr] [--verbose]\n",
		argv0);
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
	memset(o, 0, sizeof(*o));
	o->dev = "usb4_rdma0";
	o->tcp_port = 29820;
	o->ib_port = 1;
	o->gid_index = 1;
	o->iters = 100;
	o->timeout_ms = 5000;
	o->prepost_before_rtr = 1;
	o->payload = DEFAULT_PAYLOAD;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--role") && i + 1 < argc)
			o->role = argv[++i];
		else if (!strcmp(argv[i], "--dev") && i + 1 < argc)
			o->dev = argv[++i];
		else if (!strcmp(argv[i], "--connect") && i + 1 < argc)
			o->connect_host = argv[++i];
		else if (!strcmp(argv[i], "--tcp-port") && i + 1 < argc)
			o->tcp_port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--gid-index") && i + 1 < argc)
			o->gid_index = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--ib-port") && i + 1 < argc)
			o->ib_port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--iters") && i + 1 < argc)
			o->iters = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--payload") && i + 1 < argc)
			o->payload = (uint32_t)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc)
			o->timeout_ms = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--client-rtr-delay-us") &&
			 i + 1 < argc)
			o->client_rtr_delay_us = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--server-rtr-delay-us") &&
			 i + 1 < argc)
			o->server_rtr_delay_us = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--post-after-rtr"))
			o->prepost_before_rtr = 0;
		else if (!strcmp(argv[i], "--verbose"))
			o->verbose = 1;
		else
			return -1;
	}

	if (!o->role || (strcmp(o->role, "server") &&
			 strcmp(o->role, "client")))
		return -1;
	if (!strcmp(o->role, "client") && !o->connect_host)
		return -1;
	if (o->iters <= 0 || o->timeout_ms <= 0 || !o->payload ||
	    o->payload > 4096)
		return -1;
	return 0;
}

static struct ibv_context *open_context(const char *name)
{
	struct ibv_device **list;
	struct ibv_context *ctx = NULL;
	int n = 0;

	list = ibv_get_device_list(&n);
	if (!list)
		return NULL;
	for (int i = 0; i < n; i++) {
		if (strcmp(ibv_get_device_name(list[i]), name))
			continue;
		ctx = ibv_open_device(list[i]);
		break;
	}
	ibv_free_device_list(list);
	return ctx;
}

static int modify_qp_init(struct ibv_qp *qp, int port)
{
	struct ibv_qp_attr attr = {};

	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = 0;
	attr.port_num = port;
	attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE;
	return ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
			     IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

static int modify_qp_rtr_rts(struct ibv_qp *qp, int port, int gid_index,
			     const struct wire_info *local,
			     const struct wire_info *remote)
{
	struct ibv_qp_attr attr = {};
	union ibv_gid dgid;

	memcpy(&dgid, remote->gid, sizeof(remote->gid));
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = IBV_MTU_4096;
	attr.dest_qp_num = remote->qpn;
	attr.rq_psn = remote->psn;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer = 12;
	attr.ah_attr.is_global = 1;
	attr.ah_attr.grh.dgid = dgid;
	attr.ah_attr.grh.sgid_index = gid_index;
	attr.ah_attr.grh.hop_limit = 1;
	attr.ah_attr.dlid = (uint16_t)remote->lid;
	attr.ah_attr.port_num = port;
	if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_AV |
			  IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			  IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER))
		return -1;

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = 14;
	attr.retry_cnt = 7;
	attr.rnr_retry = 7;
	attr.sq_psn = local->psn;
	attr.max_rd_atomic = 1;
	if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_TIMEOUT |
			  IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
			  IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC))
		return -1;
	return 0;
}

static int post_recv_one(struct ibv_qp *qp, struct ibv_mr *mr, void *buf,
			 uint32_t len, uint64_t wr_id)
{
	struct ibv_sge sge = {
		.addr = (uintptr_t)buf,
		.length = len,
		.lkey = mr->lkey,
	};
	struct ibv_recv_wr wr = {
		.wr_id = wr_id,
		.sg_list = &sge,
		.num_sge = 1,
	};
	struct ibv_recv_wr *bad = NULL;

	return ibv_post_recv(qp, &wr, &bad);
}

static int post_send_one(struct ibv_qp *qp, struct ibv_mr *mr, void *buf,
			 uint32_t len, uint64_t wr_id)
{
	struct ibv_sge sge = {
		.addr = (uintptr_t)buf,
		.length = len,
		.lkey = mr->lkey,
	};
	struct ibv_send_wr wr = {
		.wr_id = wr_id,
		.sg_list = &sge,
		.num_sge = 1,
		.opcode = IBV_WR_SEND,
		.send_flags = IBV_SEND_SIGNALED,
	};
	struct ibv_send_wr *bad = NULL;

	return ibv_post_send(qp, &wr, &bad);
}

static int poll_for(struct ibv_cq *cq, uint64_t deadline, int want_send,
		    int want_recv, int *got_send, int *got_recv)
{
	while (now_ns() < deadline && (!*got_send || !*got_recv)) {
		struct ibv_wc wc[4];
		int n = ibv_poll_cq(cq, 4, wc);

		if (n < 0)
			return -1;
		if (!n)
			continue;
		for (int i = 0; i < n; i++) {
			if (wc[i].status != IBV_WC_SUCCESS) {
				fprintf(stderr,
					"bad wc wr_id=%" PRIu64 " status=%d opcode=%d byte_len=%u\n",
					wc[i].wr_id, wc[i].status, wc[i].opcode,
					wc[i].byte_len);
				return -1;
			}
			if (want_send && wc[i].opcode == IBV_WC_SEND)
				*got_send = 1;
			else if (want_recv && wc[i].opcode == IBV_WC_RECV)
				*got_recv = 1;
			else {
				fprintf(stderr,
					"unexpected wc wr_id=%" PRIu64 " opcode=%d status=%d\n",
					wc[i].wr_id, wc[i].opcode, wc[i].status);
				return -1;
			}
		}
	}
	return (!*got_send || !*got_recv) ? -1 : 0;
}

static int run_iter(const struct opts *o, int fd, struct ibv_pd *pd,
		    struct ibv_cq *cq, struct ibv_mr *mr,
		    void *send_buf, void *recv_buf,
		    const struct ibv_port_attr *port_attr,
		    const union ibv_gid *gid, int iter)
{
	struct ibv_qp_init_attr qp_init = {};
	struct wire_info local = {};
	struct wire_info remote = {};
	struct ibv_qp *qp;
	uint64_t deadline;
	int is_client = !strcmp(o->role, "client");
	int got_send = 0;
	int got_recv = 0;
	char ready = 1;
	char done = 1;
	int ret = 1;

	qp_init.send_cq = cq;
	qp_init.recv_cq = cq;
	qp_init.qp_type = IBV_QPT_RC;
	qp_init.cap.max_send_wr = 4;
	qp_init.cap.max_recv_wr = 4;
	qp_init.cap.max_send_sge = 1;
	qp_init.cap.max_recv_sge = 1;
	qp = ibv_create_qp(pd, &qp_init);
	if (!qp) {
		perror("ibv_create_qp");
		return 1;
	}

	memset(send_buf, is_client ? 0xc1 : 0x5e, o->payload);
	memset(recv_buf, 0, o->payload);

	local.magic = MAGIC;
	local.iter = (uint32_t)iter;
	local.qpn = qp->qp_num;
	local.psn = (uint32_t)((now_ns() + (uint64_t)iter) & 0xffffffu);
	local.lid = port_attr->lid;
	memcpy(local.gid, gid, sizeof(local.gid));

	if (send_all(fd, &local, sizeof(local)) ||
	    recv_all(fd, &remote, sizeof(remote)) ||
	    remote.magic != MAGIC || remote.iter != (uint32_t)iter) {
		fprintf(stderr, "metadata exchange failed at iter=%d\n", iter);
		goto out_qp;
	}
	if (o->verbose)
		printf("%s iter=%d local_qpn=%u remote_qpn=%u\n",
		       o->role, iter, local.qpn, remote.qpn);

	if (modify_qp_init(qp, o->ib_port)) {
		perror("ibv_modify_qp INIT");
		goto out_qp;
	}

	if (o->prepost_before_rtr &&
	    post_recv_one(qp, mr, recv_buf, o->payload, 0x1000 + iter)) {
		perror("ibv_post_recv");
		goto out_qp;
	}

	sleep_us(is_client ? o->client_rtr_delay_us : o->server_rtr_delay_us);

	if (modify_qp_rtr_rts(qp, o->ib_port, o->gid_index, &local,
			      &remote)) {
		perror("ibv_modify_qp RTR/RTS");
		goto out_qp;
	}

	if (!o->prepost_before_rtr &&
	    post_recv_one(qp, mr, recv_buf, o->payload, 0x1000 + iter)) {
		perror("ibv_post_recv");
		goto out_qp;
	}

	if (send_all(fd, &ready, sizeof(ready)) ||
	    recv_all(fd, &ready, sizeof(ready))) {
		fprintf(stderr, "ready barrier failed at iter=%d\n", iter);
		goto out_qp;
	}

	deadline = now_ns() + (uint64_t)o->timeout_ms * 1000000ull;
	if (is_client) {
		if (post_send_one(qp, mr, send_buf, o->payload, 0x2000 + iter)) {
			perror("ibv_post_send");
			goto out_qp;
		}
		if (poll_for(cq, deadline, 1, 1, &got_send, &got_recv)) {
			fprintf(stderr, "client timeout/failure at iter=%d\n", iter);
			goto out_qp;
		}
		if (send_all(fd, &done, sizeof(done)))
			goto out_qp;
	} else {
		got_recv = 0;
		got_send = 1;
		if (poll_for(cq, deadline, 0, 1, &got_send, &got_recv)) {
			fprintf(stderr, "server recv timeout/failure at iter=%d\n", iter);
			goto out_qp;
		}
		if (post_send_one(qp, mr, send_buf, o->payload, 0x3000 + iter)) {
			perror("ibv_post_send");
			goto out_qp;
		}
		got_send = 0;
		got_recv = 1;
		if (poll_for(cq, deadline, 1, 0, &got_send, &got_recv)) {
			fprintf(stderr, "server send timeout/failure at iter=%d\n", iter);
			goto out_qp;
		}
		if (recv_all(fd, &done, sizeof(done)))
			goto out_qp;
	}

	ret = 0;

out_qp:
	ibv_destroy_qp(qp);
	return ret;
}

int main(int argc, char **argv)
{
	struct ibv_context *ctx = NULL;
	struct ibv_port_attr port_attr = {};
	union ibv_gid gid;
	struct ibv_pd *pd = NULL;
	struct ibv_cq *cq = NULL;
	struct ibv_mr *mr = NULL;
	void *buf = NULL;
	void *send_buf;
	void *recv_buf;
	int listen_fd = -1;
	int fd = -1;
	struct opts o;
	int result = 1;

	if (parse_opts(argc, argv, &o)) {
		usage(argv[0]);
		return 2;
	}

	if (!strcmp(o.role, "server")) {
		listen_fd = tcp_listen(o.tcp_port);
		if (listen_fd < 0) {
			perror("listen");
			goto out;
		}
		fd = accept(listen_fd, NULL, NULL);
	} else {
		fd = tcp_connect(o.connect_host, o.tcp_port);
	}
	if (fd < 0) {
		perror("tcp");
		goto out;
	}

	ctx = open_context(o.dev);
	if (!ctx) {
		fprintf(stderr, "open device %s failed\n", o.dev);
		goto out;
	}
	if (ibv_query_port(ctx, o.ib_port, &port_attr) ||
	    ibv_query_gid(ctx, o.ib_port, o.gid_index, &gid)) {
		perror("query port/gid");
		goto out;
	}
	pd = ibv_alloc_pd(ctx);
	cq = ibv_create_cq(ctx, 32, NULL, NULL, 0);
	if (!pd || !cq)
		goto out;
	if (posix_memalign(&buf, 4096, 8192))
		goto out;
	send_buf = buf;
	recv_buf = (char *)buf + 4096;
	mr = ibv_reg_mr(pd, buf, 8192, IBV_ACCESS_LOCAL_WRITE);
	if (!mr) {
		perror("ibv_reg_mr");
		goto out;
	}

	for (int i = 0; i < o.iters; i++) {
		if (run_iter(&o, fd, pd, cq, mr, send_buf, recv_buf,
			     &port_attr, &gid, i))
			goto out;
	}

	printf("%s ok: iters=%d payload=%u prepost_before_rtr=%d client_rtr_delay_us=%d server_rtr_delay_us=%d\n",
	       o.role, o.iters, o.payload, o.prepost_before_rtr,
	       o.client_rtr_delay_us, o.server_rtr_delay_us);
	result = 0;

out:
	if (mr)
		ibv_dereg_mr(mr);
	if (cq)
		ibv_destroy_cq(cq);
	if (pd)
		ibv_dealloc_pd(pd);
	if (ctx)
		ibv_close_device(ctx);
	free(buf);
	if (fd >= 0)
		close(fd);
	if (listen_fd >= 0)
		close(listen_fd);
	return result;
}
