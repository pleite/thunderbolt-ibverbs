// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#define _POSIX_C_SOURCE 200112L
/*
 * Host-side USB4 RDMA DV SEND probe.
 *
 * TCP is used only to exchange QP metadata and ready/done bytes.  The receive
 * side uses normal verbs receive queues.  The sender transitions a normal QP
 * to RTS, attaches a DV SQ/CQ/doorbell surface, then posts SEND WQEs through
 * either the provider-private KICK ABI or the kernel poll worker.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <infiniband/ib_user_ioctl_verbs.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_user_ioctl_cmds.h>

#include "usb4_rdma_dv.h"

#define DV_SEND_MAGIC 0x44565331u /* DVS1 */
#define DV_SEND_WR_ID 0x445653454e443031ull

struct opts {
	const char *role;
	const char *dev;
	const char *connect_host;
	int port;
	int gid_index;
	int ib_port;
	int mtu;
	int timeout_ms;
	int count;
	int depth;
	int destroy_after;
	int post_delay_us;
	bool use_kick;
	bool allow_partial;
	size_t size;
};

struct peer_info {
	uint32_t magic;
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

static size_t align_up(size_t v, size_t a)
{
	return (v + a - 1) & ~(a - 1);
}

static void sleep_1ms(void)
{
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = 1000000,
	};

	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static void sleep_us(unsigned int usec)
{
	struct timespec ts = {
		.tv_sec = usec / 1000000u,
		.tv_nsec = (long)(usec % 1000000u) * 1000l,
	};

	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s --role recv|send --dev DEV --gid-index N --port P\n"
		"          [--connect HOST] [--size BYTES] [--mtu 256|512|1024|2048|4096]\n"
		"          [--count N] [--depth N] [--timeout-ms N]\n"
		"          [--kick|--no-kick] [--destroy-after N] [--post-delay-us N]\n"
		"          [--allow-partial]\n",
		argv0);
}

static int parse_int(const char *s, int *out)
{
	char *end = NULL;
	long v;

	errno = 0;
	v = strtol(s, &end, 0);
	if (errno || !end || *end || v < 0 || v > 0x7fffffff)
		return -1;
	*out = (int)v;
	return 0;
}

static int parse_size(const char *s, size_t *out)
{
	char *end = NULL;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno || !end || *end || v == 0 || v > UINT32_MAX)
		return -1;
	*out = (size_t)v;
	return 0;
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
	memset(o, 0, sizeof(*o));
	o->dev = "usb4_rdma0";
	o->port = 18517;
	o->gid_index = 1;
	o->ib_port = 1;
	o->mtu = 4096;
	o->timeout_ms = 5000;
	o->count = 1;
	o->depth = 16;
	o->use_kick = true;
	o->size = 64;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--role") && i + 1 < argc) {
			o->role = argv[++i];
		} else if (!strcmp(argv[i], "--dev") && i + 1 < argc) {
			o->dev = argv[++i];
		} else if (!strcmp(argv[i], "--connect") && i + 1 < argc) {
			o->connect_host = argv[++i];
		} else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->port))
				return -1;
		} else if (!strcmp(argv[i], "--gid-index") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->gid_index))
				return -1;
		} else if (!strcmp(argv[i], "--ib-port") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->ib_port))
				return -1;
		} else if (!strcmp(argv[i], "--mtu") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->mtu))
				return -1;
		} else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->timeout_ms))
				return -1;
		} else if (!strcmp(argv[i], "--count") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->count))
				return -1;
		} else if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->depth))
				return -1;
		} else if (!strcmp(argv[i], "--destroy-after") &&
			   i + 1 < argc) {
			if (parse_int(argv[++i], &o->destroy_after))
				return -1;
		} else if (!strcmp(argv[i], "--post-delay-us") &&
			   i + 1 < argc) {
			if (parse_int(argv[++i], &o->post_delay_us))
				return -1;
		} else if (!strcmp(argv[i], "--no-kick")) {
			o->use_kick = false;
		} else if (!strcmp(argv[i], "--kick")) {
			o->use_kick = true;
		} else if (!strcmp(argv[i], "--allow-partial")) {
			o->allow_partial = true;
		} else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
			if (parse_size(argv[++i], &o->size))
				return -1;
		} else {
			return -1;
		}
	}

	if (!o->role || (strcmp(o->role, "recv") && strcmp(o->role, "send")))
		return -1;
	if (o->port <= 0 || o->gid_index < 0 || o->ib_port <= 0 ||
	    o->timeout_ms <= 0 || o->count <= 0 ||
	    o->depth < USB4_RDMA_DV_MIN_QUEUE_ENTRIES ||
	    o->destroy_after < 0 || o->post_delay_us < 0)
		return -1;
	return 0;
}

static enum ibv_mtu mtu_enum(int mtu)
{
	switch (mtu) {
	case 256:
		return IBV_MTU_256;
	case 512:
		return IBV_MTU_512;
	case 1024:
		return IBV_MTU_1024;
	case 2048:
		return IBV_MTU_2048;
	case 4096:
	default:
		return IBV_MTU_4096;
	}
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
		if (!n)
			return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
	char *p = buf;

	while (len) {
		ssize_t n = recv(fd, p, len, MSG_WAITALL);

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
	struct sockaddr_in addr;
	int fd;
	int one = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
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
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai;
	char portbuf[16];
	int fd = -1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	snprintf(portbuf, sizeof(portbuf), "%d", port);
	if (getaddrinfo(host, portbuf, &hints, &res))
		return -1;
	for (ai = res; ai; ai = ai->ai_next) {
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

static struct ibv_context *open_dev(const char *name)
{
	struct ibv_device **list;
	struct ibv_context *ctx = NULL;
	int n = 0;

	list = ibv_get_device_list(&n);
	if (!list)
		return NULL;
	for (int i = 0; i < n; i++) {
		if (!strcmp(ibv_get_device_name(list[i]), name)) {
			ctx = ibv_open_device(list[i]);
			break;
		}
	}
	ibv_free_device_list(list);
	return ctx;
}

static int exchange_info(int fd, const struct peer_info *local,
			 struct peer_info *remote)
{
	if (send_all(fd, local, sizeof(*local)) ||
	    recv_all(fd, remote, sizeof(*remote))) {
		perror("metadata exchange");
		return -1;
	}
	if (remote->magic != local->magic) {
		fprintf(stderr, "bad remote magic 0x%x\n", remote->magic);
		return -1;
	}
	return 0;
}

static int qp_to_rts(struct ibv_qp *qp, int port, int sgid_index,
		     const union ibv_gid *dgid, uint32_t dlid,
		     uint32_t dest_qpn, uint32_t local_psn,
		     uint32_t remote_psn, enum ibv_mtu path_mtu)
{
	struct ibv_qp_attr a;
	int ret;

	memset(&a, 0, sizeof(a));
	a.qp_state = IBV_QPS_INIT;
	a.pkey_index = 0;
	a.port_num = (uint8_t)port;
	a.qp_access_flags = 0;
	errno = 0;
	ret = ibv_modify_qp(qp, &a, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
			    IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
	if (ret) {
		perror("modify INIT");
		return ret;
	}

	memset(&a, 0, sizeof(a));
	a.qp_state = IBV_QPS_RTR;
	a.path_mtu = path_mtu;
	a.rq_psn = remote_psn;
	a.dest_qp_num = dest_qpn;
	a.ah_attr.dlid = (uint16_t)dlid;
	a.ah_attr.sl = 0;
	a.ah_attr.src_path_bits = 0;
	a.ah_attr.port_num = (uint8_t)port;
	a.ah_attr.is_global = 1;
	a.ah_attr.grh.dgid = *dgid;
	a.ah_attr.grh.sgid_index = sgid_index;
	a.ah_attr.grh.hop_limit = 1;
	errno = 0;
	ret = ibv_modify_qp(qp, &a, IBV_QP_STATE | IBV_QP_AV |
			    IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			    IBV_QP_RQ_PSN);
	if (ret) {
		fprintf(stderr,
			"modify RTR ret=%d errno=%d dest_qpn=%u sgid_index=%d dlid=%u\n",
			ret, errno, dest_qpn, sgid_index, dlid);
		perror("modify RTR");
		return ret;
	}

	memset(&a, 0, sizeof(a));
	a.qp_state = IBV_QPS_RTS;
	a.sq_psn = local_psn;
	errno = 0;
	ret = ibv_modify_qp(qp, &a, IBV_QP_STATE | IBV_QP_SQ_PSN);
	if (ret)
		perror("modify RTS");
	return ret;
}

static int post_recv_slot(struct ibv_qp *qp, struct ibv_mr *mr, char *buf,
			  size_t stride, size_t size, int slot)
{
	struct ibv_sge sge;
	struct ibv_recv_wr wr;
	struct ibv_recv_wr *bad = NULL;

	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)(buf + (size_t)slot * stride);
	sge.length = (uint32_t)size;
	sge.lkey = mr->lkey;
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = (uint64_t)slot;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	return ibv_post_recv(qp, &wr, &bad);
}

static void store_le64(char *p, uint64_t v)
{
	for (size_t i = 0; i < sizeof(v); i++)
		p[i] = (char)((v >> (i * 8)) & 0xff);
}

static uint64_t load_le64(const char *p)
{
	uint64_t v = 0;

	for (size_t i = 0; i < sizeof(v); i++)
		v |= (uint64_t)(unsigned char)p[i] << (i * 8);
	return v;
}

static unsigned char pattern_byte(uint64_t seq, size_t off)
{
	return (unsigned char)(0x40u ^ (unsigned int)seq ^
			       (unsigned int)(seq >> 8) ^
			       (unsigned int)(seq >> 16) ^
			       (unsigned int)(off * 131u) ^
			       (unsigned int)(off >> 8));
}

static void fill_pattern(char *p, size_t size, uint64_t seq)
{
	for (size_t i = 0; i < size; i++)
		p[i] = (char)pattern_byte(seq, i);
	if (size >= sizeof(seq))
		store_le64(p, seq);
}

static int check_pattern(const char *p, size_t size, uint64_t expected_seq)
{
	uint64_t observed_seq = size >= sizeof(observed_seq) ? load_le64(p) : 0;

	for (size_t i = 0; i < size; i++) {
		unsigned char got = (unsigned char)p[i];
		unsigned char want;

		if (i < sizeof(expected_seq) && size >= sizeof(expected_seq))
			want = (unsigned char)((expected_seq >> (i * 8)) & 0xff);
		else
			want = pattern_byte(expected_seq, i);

		if (got != want) {
			fprintf(stderr,
				"payload mismatch expected_seq=%" PRIu64 " observed_seq=%" PRIu64 " off=%zu got=0x%02x want=0x%02x\n",
				expected_seq, observed_seq, i, got, want);
			return -1;
		}
	}
	return 0;
}

static int query_caps(struct ibv_context *ctx,
		      struct usb4_rdma_dv_query_caps_resp *resp)
{
	struct {
		struct ib_uverbs_ioctl_hdr hdr;
		struct ib_uverbs_attr attrs[1];
	} cmd = {
		.hdr = {
			.length = sizeof(cmd),
			.object_id = USB4_RDMA_DV_OBJECT_DEVICE,
			.method_id = USB4_RDMA_DV_METHOD_QUERY_CAPS,
			.num_attrs = 1,
			.driver_id = RDMA_DRIVER_UNKNOWN,
		},
		.attrs = {
			{
				.attr_id = USB4_RDMA_DV_ATTR_QUERY_CAPS_RESP,
				.len = sizeof(*resp),
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = (uintptr_t)resp,
			},
		},
	};

	memset(resp, 0, sizeof(*resp));
	if (ioctl(ctx->cmd_fd, RDMA_VERBS_IOCTL, &cmd) < 0)
		return errno;
	return 0;
}

static int create_queue(struct ibv_qp *qp,
			const struct usb4_rdma_dv_queue_create *req,
			struct usb4_rdma_dv_queue_resp *resp)
{
	struct {
		struct ib_uverbs_ioctl_hdr hdr;
		struct ib_uverbs_attr attrs[3];
	} cmd = {
		.hdr = {
			.length = sizeof(cmd),
			.object_id = USB4_RDMA_DV_OBJECT_DEVICE,
			.method_id = USB4_RDMA_DV_METHOD_CREATE_QUEUE,
			.num_attrs = 3,
			.driver_id = RDMA_DRIVER_UNKNOWN,
		},
		.attrs = {
			{
				.attr_id = USB4_RDMA_DV_ATTR_CREATE_QUEUE_QP,
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = qp->handle,
			},
			{
				.attr_id = USB4_RDMA_DV_ATTR_CREATE_QUEUE_REQ,
				.len = sizeof(*req),
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = (uintptr_t)req,
			},
			{
				.attr_id = USB4_RDMA_DV_ATTR_CREATE_QUEUE_RESP,
				.len = sizeof(*resp),
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = (uintptr_t)resp,
			},
		},
	};

	memset(resp, 0, sizeof(*resp));
	if (ioctl(qp->context->cmd_fd, RDMA_VERBS_IOCTL, &cmd) < 0)
		return errno;
	return 0;
}

static int destroy_queue(struct ibv_qp *qp)
{
	struct {
		struct ib_uverbs_ioctl_hdr hdr;
		struct ib_uverbs_attr attrs[1];
	} cmd = {
		.hdr = {
			.length = sizeof(cmd),
			.object_id = USB4_RDMA_DV_OBJECT_DEVICE,
			.method_id = USB4_RDMA_DV_METHOD_DESTROY_QUEUE,
			.num_attrs = 1,
			.driver_id = RDMA_DRIVER_UNKNOWN,
		},
		.attrs = {
			{
				.attr_id = USB4_RDMA_DV_ATTR_DESTROY_QUEUE_QP,
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = qp->handle,
			},
		},
	};

	if (ioctl(qp->context->cmd_fd, RDMA_VERBS_IOCTL, &cmd) < 0)
		return errno;
	return 0;
}

static int kick_queue(struct ibv_qp *qp, const struct usb4_rdma_dv_kick *req)
{
	struct {
		struct ib_uverbs_ioctl_hdr hdr;
		struct ib_uverbs_attr attrs[2];
	} cmd = {
		.hdr = {
			.length = sizeof(cmd),
			.object_id = USB4_RDMA_DV_OBJECT_DEVICE,
			.method_id = USB4_RDMA_DV_METHOD_KICK,
			.num_attrs = 2,
			.driver_id = RDMA_DRIVER_UNKNOWN,
		},
		.attrs = {
			{
				.attr_id = USB4_RDMA_DV_ATTR_KICK_QP,
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = qp->handle,
			},
			{
				.attr_id = USB4_RDMA_DV_ATTR_KICK_REQ,
				.len = sizeof(*req),
				.flags = UVERBS_ATTR_F_MANDATORY,
				.data = (uintptr_t)req,
			},
		},
	};

	if (ioctl(qp->context->cmd_fd, RDMA_VERBS_IOCTL, &cmd) < 0)
		return errno;
	return 0;
}

static int alloc_aligned(size_t alignment, size_t size, void **ptr)
{
	int ret;

	*ptr = NULL;
	ret = posix_memalign(ptr, alignment, size);
	if (ret)
		return ret;
	memset(*ptr, 0, size);
	return 0;
}

static void store_release_u32(uint32_t *ptr, uint32_t value)
{
	__atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static uint32_t load_acquire_u32(uint32_t *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static int poll_dv_cqe(struct usb4_rdma_dv_doorbell *doorbell,
		       struct usb4_rdma_dv_cqe *cq, uint32_t cq_entries,
		       uint32_t generation, uint32_t completed,
		       uint64_t wr_id, uint32_t byte_len, int timeout_ms)
{
	uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ull;

	while (now_ns() < deadline) {
		uint32_t tail = load_acquire_u32(&doorbell->consumer.cq_tail);
		struct usb4_rdma_dv_cqe *cqe;

		if (usb4_rdma_dv_tail_generation(tail) == generation &&
		    usb4_rdma_dv_tail_index(tail) >= completed + 1) {
			cqe = &cq[completed % cq_entries];
			if (completed < 16 || ((completed + 1) % 1024) == 0)
				printf("dv_send completed=%u wr_id=0x%016" PRIx64
				       " status=%u opcode=%u byte_len=%u sq_head=%u cq_tail=%u qp_state=%u\n",
				       completed + 1, (uint64_t)cqe->wr_id,
				       cqe->status, cqe->opcode, cqe->byte_len,
				       usb4_rdma_dv_tail_index(load_acquire_u32(
					       &doorbell->consumer.sq_head)),
				       usb4_rdma_dv_tail_index(tail),
				       load_acquire_u32(
					       &doorbell->consumer.qp_state));
			if (cqe->wr_id != wr_id ||
			    cqe->status != USB4_RDMA_DV_CQE_SUCCESS ||
			    cqe->opcode != USB4_RDMA_DV_WQE_SEND ||
			    cqe->byte_len != byte_len) {
				fprintf(stderr,
					"unexpected DV CQE completed=%u wr_id=0x%016" PRIx64 " status=%u opcode=%u byte_len=%u\n",
					completed, (uint64_t)cqe->wr_id,
					cqe->status, cqe->opcode,
					cqe->byte_len);
				return -1;
			}
			return 0;
		}
		sleep_1ms();
	}

	fprintf(stderr, "timed out waiting for DV CQE completed=%u\n",
		completed);
	return -1;
}

static int run_receiver(struct opts *o, int sock, struct ibv_qp *qp,
			struct ibv_cq *cq, struct ibv_mr *mr, char *buf,
			size_t stride)
{
	struct ibv_wc wc;
	char ready = 'R';
	char ack;
	char done = 'D';
	char peer_done;
	uint64_t deadline;
	uint64_t start_ns;
	uint64_t completed = 0;

	for (int i = 0; i < o->depth; i++) {
		if (post_recv_slot(qp, mr, buf, stride, o->size, i)) {
			perror("ibv_post_recv");
			return 1;
		}
	}
	if (send_all(sock, &ready, 1)) {
		perror("ready send");
		return 1;
	}
	if (recv_all(sock, &ack, 1) || ack != 'A') {
		fprintf(stderr, "sender did not ack ready\n");
		return 1;
	}

	deadline = now_ns() + (uint64_t)o->timeout_ms * 1000000ull;
	start_ns = now_ns();
	while (completed < (uint64_t)o->count && now_ns() < deadline) {
		int n = ibv_poll_cq(cq, 1, &wc);
		int slot;

		if (n < 0) {
			fprintf(stderr, "ibv_poll_cq failed: %d\n", n);
			return 1;
		}
		if (!n) {
			if (o->allow_partial) {
				ssize_t r = recv(sock, &peer_done, 1,
						 MSG_DONTWAIT);

				if (r == 1 && peer_done == 'D') {
					double secs;
					double gbps;

					secs = (double)(now_ns() - start_ns) /
					       1000000000.0;
					gbps = secs > 0.0 ?
					       ((double)completed *
						(double)o->size * 8.0) /
						       secs / 1000000000.0 :
					       0.0;
					printf("recv_partial completed=%" PRIu64
					       "/%d size=%zu elapsed_sec=%.6f gbps=%.3f\n",
					       completed, o->count, o->size,
					       secs, gbps);
					if (send_all(sock, &done, 1)) {
						perror("done send");
						return 1;
					}
					return 0;
				}
				if (r < 0 && errno != EAGAIN &&
				    errno != EWOULDBLOCK && errno != EINTR) {
					perror("peer done recv");
					return 1;
				}
				if (r == 1) {
					fprintf(stderr,
						"unexpected peer done byte '%c'\n",
						peer_done);
					return 1;
				}
				if (r == 0) {
					fprintf(stderr, "peer closed early\n");
					return 1;
				}
			}
			sleep_1ms();
			continue;
		}
		if (wc.status != IBV_WC_SUCCESS) {
			fprintf(stderr,
				"recv wc error wr_id=%" PRIu64 " status=%u opcode=%u byte_len=%u\n",
				wc.wr_id, wc.status, wc.opcode, wc.byte_len);
			return 1;
		}
		if (wc.byte_len != o->size) {
			fprintf(stderr, "recv byte_len mismatch got=%u want=%zu\n",
				wc.byte_len, o->size);
			return 1;
		}
		slot = (int)wc.wr_id;
		if (slot < 0 || slot >= o->depth) {
			fprintf(stderr, "recv bad wr_id slot=%" PRIu64 "\n",
				wc.wr_id);
			return 1;
		}
		if (check_pattern(buf + (size_t)slot * stride, o->size,
				  completed))
			return 1;
		completed++;
		if ((completed % 1024) == 0 ||
		    completed == (uint64_t)o->count || o->count <= 16)
			printf("recv_payload completed=%" PRIu64
			       "/%d wr_id=%" PRIu64
			       " status=%u opcode=%u byte_len=%u ok\n",
			       completed, o->count, wc.wr_id, wc.status,
			       wc.opcode, wc.byte_len);
		if (completed < (uint64_t)o->count &&
		    post_recv_slot(qp, mr, buf, stride, o->size, slot)) {
			perror("ibv_post_recv(repost)");
			return 1;
		}
	}

	if (completed != (uint64_t)o->count) {
		fprintf(stderr, "timed out waiting for receive CQE completed=%" PRIu64
			"/%d\n",
			completed, o->count);
		return 1;
	}

	{
		double secs = (double)(now_ns() - start_ns) / 1000000000.0;
		double gbps = secs > 0.0 ?
			      ((double)completed * (double)o->size * 8.0) /
				      secs / 1000000000.0 :
			      0.0;

		printf("recv_complete count=%" PRIu64
		       " size=%zu elapsed_sec=%.6f gbps=%.3f\n",
		       completed, o->size, secs, gbps);
	}
	if (send_all(sock, &done, 1)) {
		perror("done send");
		return 1;
	}
	if (recv_all(sock, &peer_done, 1) || peer_done != 'D') {
		fprintf(stderr, "sender did not signal done\n");
		return 1;
	}
	return 0;
}

static int run_sender(struct opts *o, int sock, struct ibv_context *ctx,
		      struct ibv_pd *pd, struct ibv_qp *qp,
		      struct ibv_mr *payload_mr, char *payload, size_t stride)
{
	struct usb4_rdma_dv_query_caps_resp caps;
	struct usb4_rdma_dv_queue_create req = {};
	struct usb4_rdma_dv_queue_resp resp = {};
	struct usb4_rdma_dv_wqe *sq = NULL;
	struct usb4_rdma_dv_cqe *dv_cq = NULL;
	struct usb4_rdma_dv_doorbell *doorbell = NULL;
	struct ibv_mr *sq_mr = NULL;
	struct ibv_mr *cq_mr = NULL;
	struct ibv_mr *doorbell_mr = NULL;
	size_t sq_entries;
	size_t cq_entries;
	size_t sq_bytes;
	size_t cq_bytes;
	int access = IBV_ACCESS_LOCAL_WRITE;
	char ready;
	char ack = 'A';
	char done;
	char peer_done = 'D';
	uint64_t posted = 0;
	uint64_t completed = 0;
	uint64_t start_ns = 0;
	uint64_t deadline;
	bool destroyed = false;
	int ret = 1;
	int err;

	err = query_caps(ctx, &caps);
	if (err) {
		fprintf(stderr, "QUERY_CAPS failed: %s (%d)\n", strerror(err),
			err);
		goto out;
	}
	if (caps.abi_version != USB4_RDMA_DV_ABI_VERSION ||
	    !(caps.caps & USB4_RDMA_DV_CAP_SEND)) {
		fprintf(stderr, "unsupported DV caps abi=%u caps=0x%x\n",
			caps.abi_version, caps.caps);
		goto out;
	}
	sq_entries = (size_t)o->depth;
	cq_entries = (size_t)o->depth;
	if (sq_entries > caps.max_sq_entries)
		sq_entries = caps.max_sq_entries;
	if (cq_entries > caps.max_cq_entries)
		cq_entries = caps.max_cq_entries;
	if (sq_entries < USB4_RDMA_DV_MIN_QUEUE_ENTRIES ||
	    cq_entries < USB4_RDMA_DV_MIN_QUEUE_ENTRIES) {
		fprintf(stderr, "invalid caps sq=%zu cq=%zu\n", sq_entries,
			cq_entries);
		goto out;
	}
	sq_bytes = sq_entries * caps.wqe_size;
	cq_bytes = cq_entries * caps.cqe_size;

	err = alloc_aligned(caps.wqe_size, sq_bytes, (void **)&sq);
	if (err) {
		fprintf(stderr, "alloc SQ: %s\n", strerror(err));
		goto out;
	}
	err = alloc_aligned(caps.cqe_size, cq_bytes, (void **)&dv_cq);
	if (err) {
		fprintf(stderr, "alloc CQ: %s\n", strerror(err));
		goto out;
	}
	err = alloc_aligned(caps.doorbell_page_size, caps.doorbell_page_size,
			    (void **)&doorbell);
	if (err) {
		fprintf(stderr, "alloc doorbell: %s\n", strerror(err));
		goto out;
	}

	sq_mr = ibv_reg_mr(pd, sq, sq_bytes, access);
	cq_mr = ibv_reg_mr(pd, dv_cq, cq_bytes, access);
	doorbell_mr = ibv_reg_mr(pd, doorbell, caps.doorbell_page_size,
				 access);
	if (!sq_mr || !cq_mr || !doorbell_mr) {
		fprintf(stderr, "ibv_reg_mr(DV queues): %s\n", strerror(errno));
		goto out;
	}

	req.abi_version = USB4_RDMA_DV_ABI_VERSION;
	req.sq_addr = (uintptr_t)sq;
	req.cq_addr = (uintptr_t)dv_cq;
	req.doorbell_addr = (uintptr_t)doorbell;
	req.sq_entries = (uint32_t)sq_entries;
	req.cq_entries = (uint32_t)cq_entries;
	req.sq_stride = caps.wqe_size;
	req.cq_stride = caps.cqe_size;
	err = create_queue(qp, &req, &resp);
	if (err) {
		fprintf(stderr, "CREATE_QUEUE failed: %s (%d)\n",
			strerror(err), err);
		goto out;
	}
	printf("create_queue qp_num=%u generation=%u sq_entries=%u cq_entries=%u\n",
	       resp.qp_num, resp.generation, req.sq_entries, req.cq_entries);

	store_release_u32(&doorbell->producer.generation, resp.generation);
	store_release_u32(&doorbell->producer.cq_head,
			  usb4_rdma_dv_tail_pack(0, resp.generation));
	store_release_u32(&doorbell->producer.sq_tail,
			  usb4_rdma_dv_tail_pack(0, resp.generation));

	if (recv_all(sock, &ready, 1) || ready != 'R') {
		fprintf(stderr, "receiver did not signal ready\n");
		goto out_destroy;
	}
	if (send_all(sock, &ack, 1)) {
		perror("ack send");
		goto out_destroy;
	}

	printf("send_start count=%d depth=%d queue_entries=%zu mode=%s destroy_after=%d post_delay_us=%d\n",
	       o->count, o->depth, sq_entries, o->use_kick ? "kick" :
	       "poll", o->destroy_after, o->post_delay_us);
	start_ns = now_ns();
	deadline = start_ns + (uint64_t)o->timeout_ms * 1000000ull;
	while (completed < (uint64_t)o->count && now_ns() < deadline) {
		while (posted < (uint64_t)o->count &&
		       posted - completed < sq_entries) {
			struct usb4_rdma_dv_wqe *wqe;
			uint64_t seq = posted;
			uint32_t tail;
			size_t slot = (size_t)(posted % sq_entries);

			fill_pattern(payload + slot * stride, o->size, seq);
			wqe = &sq[slot];
			memset(wqe, 0, sizeof(*wqe));
			wqe->opcode = USB4_RDMA_DV_WQE_SEND;
			wqe->flags = USB4_RDMA_DV_WQE_F_SIGNALED;
			wqe->length = (uint32_t)o->size;
			wqe->wr_id = DV_SEND_WR_ID + seq;
			wqe->local_addr = (uintptr_t)(payload + slot * stride);
			wqe->lkey = payload_mr->lkey;
			wqe->generation = resp.generation;
			__atomic_thread_fence(__ATOMIC_RELEASE);
			tail = usb4_rdma_dv_tail_pack((uint32_t)(posted + 1),
						      resp.generation);
			store_release_u32(&doorbell->producer.sq_tail, tail);
			posted++;

			if (o->use_kick) {
				struct usb4_rdma_dv_kick kick = {
					.sq_tail = tail,
				};

				err = kick_queue(qp, &kick);
				if (err) {
					fprintf(stderr,
						"KICK failed: %s (%d)\n",
						strerror(err), err);
					goto out_destroy;
				}
			}

			if (o->destroy_after > 0 &&
			    posted >= (uint64_t)o->destroy_after) {
				printf("destroy_after_published posted=%" PRIu64
				       " completed=%" PRIu64 "\n",
				       posted, completed);
				err = destroy_queue(qp);
				destroyed = true;
				if (err) {
					fprintf(stderr,
						"DESTROY_QUEUE failed: %s (%d)\n",
						strerror(err), err);
					goto out;
				}
				printf("destroy_queue ok\n");
				if (send_all(sock, &peer_done, 1)) {
					perror("done send");
					goto out;
				}
				if (recv_all(sock, &done, 1) || done != 'D') {
					fprintf(stderr,
						"receiver did not signal done\n");
					goto out;
				}
				ret = 0;
				goto out;
			}

			if (o->post_delay_us)
				sleep_us((unsigned int)o->post_delay_us);
		}

		if (completed < posted) {
			if (poll_dv_cqe(doorbell, dv_cq, (uint32_t)cq_entries,
					resp.generation, (uint32_t)completed,
					DV_SEND_WR_ID + completed,
					(uint32_t)o->size, o->timeout_ms))
				goto out_destroy;
			completed++;
			store_release_u32(&doorbell->producer.cq_head,
					  usb4_rdma_dv_tail_pack(
						  (uint32_t)completed,
						  resp.generation));
		}
	}

	if (completed != (uint64_t)o->count) {
		fprintf(stderr, "timed out waiting for DV completions posted=%" PRIu64
			" completed=%" PRIu64 "/%d\n",
			posted, completed, o->count);
		goto out_destroy;
	}

	{
		double secs = (double)(now_ns() - start_ns) / 1000000000.0;
		double gbps = secs > 0.0 ?
			      ((double)completed * (double)o->size * 8.0) /
				      secs / 1000000000.0 :
			      0.0;

		printf("send_complete count=%" PRIu64
		       " size=%zu elapsed_sec=%.6f gbps=%.3f mode=%s\n",
		       completed, o->size, secs, gbps,
		       o->use_kick ? "kick" : "poll");
	}

	if (recv_all(sock, &done, 1) || done != 'D') {
		fprintf(stderr, "receiver did not signal done\n");
		goto out_destroy;
	}
	if (send_all(sock, &peer_done, 1)) {
		perror("done send");
		goto out_destroy;
	}
	ret = 0;

out_destroy:
	if (!destroyed) {
		err = destroy_queue(qp);
		if (err) {
			fprintf(stderr, "DESTROY_QUEUE failed: %s (%d)\n",
				strerror(err), err);
			ret = 1;
		} else {
			printf("destroy_queue ok\n");
		}
	}
out:
	if (doorbell_mr && ibv_dereg_mr(doorbell_mr))
		fprintf(stderr, "ibv_dereg_mr(doorbell): %s\n", strerror(errno));
	if (cq_mr && ibv_dereg_mr(cq_mr))
		fprintf(stderr, "ibv_dereg_mr(CQ): %s\n", strerror(errno));
	if (sq_mr && ibv_dereg_mr(sq_mr))
		fprintf(stderr, "ibv_dereg_mr(SQ): %s\n", strerror(errno));
	free(doorbell);
	free(dv_cq);
	free(sq);
	return ret;
}

int main(int argc, char **argv)
{
	struct opts o;
	struct ibv_context *ctx = NULL;
	struct ibv_port_attr port_attr;
	union ibv_gid local_gid, remote_gid;
	struct ibv_pd *pd = NULL;
	struct ibv_cq *cq = NULL;
	struct ibv_qp *qp = NULL;
	struct ibv_qp_init_attr qpia;
	struct ibv_mr *mr = NULL;
	struct peer_info local, remote;
	int listen_fd = -1;
	int sock = -1;
	uint32_t psn;
	long sys_page_size;
	size_t page_size;
	size_t stride;
	size_t bufsz;
	char *buf = NULL;
	int is_sender;
	int ret = 1;

	if (parse_opts(argc, argv, &o)) {
		usage(argv[0]);
		return 2;
	}
	is_sender = !strcmp(o.role, "send");
	setvbuf(stdout, NULL, _IOLBF, 0);

	ctx = open_dev(o.dev);
	if (!ctx) {
		fprintf(stderr, "failed to open RDMA device %s\n", o.dev);
		return 1;
	}
	if (ibv_query_port(ctx, (uint8_t)o.ib_port, &port_attr)) {
		perror("ibv_query_port");
		goto out_ctx;
	}
	if (ibv_query_gid(ctx, o.ib_port, o.gid_index, &local_gid)) {
		perror("ibv_query_gid");
		goto out_ctx;
	}

	pd = ibv_alloc_pd(ctx);
	if (!pd) {
		perror("ibv_alloc_pd");
		goto out_ctx;
	}

	sys_page_size = sysconf(_SC_PAGESIZE);
	if (sys_page_size <= 0) {
		perror("sysconf(_SC_PAGESIZE)");
		goto out_pd;
	}
	page_size = (size_t)sys_page_size;
	stride = align_up(o.size, page_size);
	if ((size_t)o.depth > SIZE_MAX / stride) {
		fprintf(stderr, "payload buffer size overflow\n");
		goto out_pd;
	}
	bufsz = align_up(stride * (size_t)o.depth, page_size);
	if (posix_memalign((void **)&buf, page_size, bufsz)) {
		fprintf(stderr, "posix_memalign payload failed\n");
		goto out_pd;
	}
	memset(buf, is_sender ? 0x5a : 0xcc, bufsz);

	mr = ibv_reg_mr(pd, buf, bufsz, IBV_ACCESS_LOCAL_WRITE);
	if (!mr) {
		perror("ibv_reg_mr(payload)");
		goto out_buf;
	}

	cq = ibv_create_cq(ctx, o.depth + 16, NULL, NULL, 0);
	if (!cq) {
		perror("ibv_create_cq");
		goto out_mr;
	}

	memset(&qpia, 0, sizeof(qpia));
	qpia.send_cq = cq;
	qpia.recv_cq = cq;
	qpia.cap.max_send_wr = o.depth;
	qpia.cap.max_recv_wr = o.depth;
	qpia.cap.max_send_sge = 1;
	qpia.cap.max_recv_sge = 1;
	qpia.qp_type = IBV_QPT_UC;
	qp = ibv_create_qp(pd, &qpia);
	if (!qp) {
		perror("ibv_create_qp");
		goto out_cq;
	}

	psn = (uint32_t)(now_ns() ^ (uint64_t)getpid()) & 0xffffffu;
	memset(&local, 0, sizeof(local));
	local.magic = DV_SEND_MAGIC;
	local.qpn = qp->qp_num;
	local.psn = psn;
	local.lid = port_attr.lid;
	memcpy(local.gid, local_gid.raw, sizeof(local.gid));

	if (o.connect_host) {
		sock = tcp_connect(o.connect_host, o.port);
		if (sock < 0) {
			perror("tcp connect");
			goto out_qp;
		}
	} else {
		listen_fd = tcp_listen(o.port);
		if (listen_fd < 0) {
			perror("tcp listen");
			goto out_qp;
		}
		printf("listening on TCP port %d\n", o.port);
		sock = accept(listen_fd, NULL, NULL);
		if (sock < 0) {
			perror("accept");
			goto out_qp;
		}
		close(listen_fd);
		listen_fd = -1;
	}

	if (exchange_info(sock, &local, &remote))
		goto out_qp;
	memcpy(remote_gid.raw, remote.gid, sizeof(remote.gid));
	printf("%s local_qpn=%u remote_qpn=%u size=%zu gid_index=%d\n",
	       o.role, local.qpn, remote.qpn, o.size, o.gid_index);

	if (qp_to_rts(qp, o.ib_port, o.gid_index, &remote_gid, remote.lid,
		      remote.qpn, local.psn, remote.psn, mtu_enum(o.mtu)))
		goto out_qp;

	if (is_sender)
		ret = run_sender(&o, sock, ctx, pd, qp, mr, buf, stride);
	else
		ret = run_receiver(&o, sock, qp, cq, mr, buf, stride);

out_qp:
	if (listen_fd >= 0)
		close(listen_fd);
	if (sock >= 0)
		close(sock);
	if (qp && ibv_destroy_qp(qp))
		fprintf(stderr, "ibv_destroy_qp: %s\n", strerror(errno));
out_cq:
	if (cq && ibv_destroy_cq(cq))
		fprintf(stderr, "ibv_destroy_cq: %s\n", strerror(errno));
out_mr:
	if (mr && ibv_dereg_mr(mr))
		fprintf(stderr, "ibv_dereg_mr(payload): %s\n", strerror(errno));
out_buf:
	free(buf);
out_pd:
	if (pd && ibv_dealloc_pd(pd))
		fprintf(stderr, "ibv_dealloc_pd: %s\n", strerror(errno));
out_ctx:
	if (ctx && ibv_close_device(ctx))
		fprintf(stderr, "ibv_close_device: %s\n", strerror(errno));
	return ret;
}
