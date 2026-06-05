// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#define _POSIX_C_SOURCE 200112L
/*
 * Minimal host-side USB4 RDMA DV ioctl probe.
 *
 * This intentionally uses the raw RDMA_VERBS_IOCTL ABI instead of rdma-core's
 * private execute_ioctl() helper, so it can be built as an ordinary test
 * binary outside the provider tree.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <infiniband/ib_user_ioctl_verbs.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_user_ioctl_cmds.h>

#include "usb4_rdma_dv.h"

static bool device_name_matches(const char *name, const char *wanted)
{
	return strcmp(name, wanted) == 0 ||
	       strncmp(name, "usb4_rdma", strlen("usb4_rdma")) == 0 ||
	       strncmp(name, "usb4_apple", strlen("usb4_apple")) == 0;
}

static struct ibv_device *find_device(struct ibv_device **list, int count,
				      const char *wanted)
{
	struct ibv_device *fallback = NULL;
	int i;

	for (i = 0; i < count; i++) {
		const char *name = ibv_get_device_name(list[i]);

		if (strcmp(name, wanted) == 0)
			return list[i];
		if (!fallback && device_name_matches(name, wanted))
			fallback = list[i];
	}
	return fallback;
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

static void print_doorbell_field(const char *name, const char *writer,
				 const char *reader, size_t offset, size_t size)
{
	printf("doorbell_field name=%s offset=%zu size=%zu writer=%s reader=%s\n",
	       name, offset, size, writer, reader);
}

static void print_doorbell_layout(void)
{
	size_t producer_off = offsetof(struct usb4_rdma_dv_doorbell, producer);
	size_t consumer_off = offsetof(struct usb4_rdma_dv_doorbell, consumer);

	printf("doorbell_layout record_size=%zu producer_line_offset=%zu producer_line_size=%zu consumer_line_offset=%zu consumer_line_size=%zu\n",
	       sizeof(struct usb4_rdma_dv_doorbell), producer_off,
	       sizeof(struct usb4_rdma_dv_doorbell_producer_line),
	       consumer_off, sizeof(struct usb4_rdma_dv_doorbell_consumer_line));
	print_doorbell_field(
		"producer.sq_tail", "gpu", "kernel",
		producer_off +
			offsetof(struct usb4_rdma_dv_doorbell_producer_line, sq_tail),
		sizeof(((struct usb4_rdma_dv_doorbell_producer_line *)0)->sq_tail));
	print_doorbell_field(
		"producer.cq_head", "gpu", "kernel",
		producer_off +
			offsetof(struct usb4_rdma_dv_doorbell_producer_line, cq_head),
		sizeof(((struct usb4_rdma_dv_doorbell_producer_line *)0)->cq_head));
	print_doorbell_field(
		"producer.generation", "gpu", "kernel",
		producer_off +
			offsetof(struct usb4_rdma_dv_doorbell_producer_line,
				 generation),
		sizeof(((struct usb4_rdma_dv_doorbell_producer_line *)0)
			       ->generation));
	print_doorbell_field(
		"consumer.sq_head", "kernel", "gpu",
		consumer_off +
			offsetof(struct usb4_rdma_dv_doorbell_consumer_line, sq_head),
		sizeof(((struct usb4_rdma_dv_doorbell_consumer_line *)0)->sq_head));
	print_doorbell_field(
		"consumer.cq_tail", "kernel", "gpu",
		consumer_off +
			offsetof(struct usb4_rdma_dv_doorbell_consumer_line, cq_tail),
		sizeof(((struct usb4_rdma_dv_doorbell_consumer_line *)0)->cq_tail));
	print_doorbell_field(
		"consumer.qp_state", "kernel", "gpu",
		consumer_off +
			offsetof(struct usb4_rdma_dv_doorbell_consumer_line,
				 qp_state),
		sizeof(((struct usb4_rdma_dv_doorbell_consumer_line *)0)
			       ->qp_state));
	print_doorbell_field(
		"consumer.generation", "kernel", "gpu",
		consumer_off +
			offsetof(struct usb4_rdma_dv_doorbell_consumer_line,
				 generation),
		sizeof(((struct usb4_rdma_dv_doorbell_consumer_line *)0)
			       ->generation));
	printf("wqe_field name=generation offset=%zu size=%zu\n",
	       offsetof(struct usb4_rdma_dv_wqe, generation),
	       sizeof(((struct usb4_rdma_dv_wqe *)0)->generation));
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

static void free_aligned(void *ptr)
{
	free(ptr);
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

static uint8_t next_generation(uint32_t generation)
{
	uint32_t mask = (1u << USB4_RDMA_DV_TAIL_GENERATION_BITS) - 1;
	uint32_t next = (generation + 1) & mask;

	return next ? next : 1;
}

static int expect_cqe(const char *label, struct usb4_rdma_dv_cqe *cqe,
		      struct usb4_rdma_dv_doorbell *doorbell, uint32_t index,
		      uint32_t generation, uint64_t wr_id, uint32_t status,
		      uint32_t opcode)
{
	uint32_t cq_tail = load_acquire_u32(&doorbell->consumer.cq_tail);
	uint32_t sq_head = load_acquire_u32(&doorbell->consumer.sq_head);

	printf("%s wr_id=0x%016" PRIx64 " status=%u opcode=%u sq_head=%u cq_tail=%u qp_state=%u\n",
	       label, cqe->wr_id, cqe->status, cqe->opcode,
	       usb4_rdma_dv_tail_index(sq_head),
	       usb4_rdma_dv_tail_index(cq_tail),
	       load_acquire_u32(&doorbell->consumer.qp_state));

	if (usb4_rdma_dv_tail_generation(cq_tail) != generation ||
	    usb4_rdma_dv_tail_index(cq_tail) != index + 1) {
		fprintf(stderr, "%s: unexpected cq_tail=0x%08x\n", label,
			cq_tail);
		return 1;
	}
	if (usb4_rdma_dv_tail_generation(sq_head) != generation ||
	    usb4_rdma_dv_tail_index(sq_head) != index + 1) {
		fprintf(stderr, "%s: unexpected sq_head=0x%08x\n", label,
			sq_head);
		return 1;
	}
	if (cqe->wr_id != wr_id || cqe->status != status ||
	    cqe->opcode != opcode) {
		fprintf(stderr,
			"%s: unexpected CQE wr_id=0x%016" PRIx64 " status=%u opcode=%u\n",
			label, cqe->wr_id, cqe->status, cqe->opcode);
		return 1;
	}
	return 0;
}

static int run_kick_smoke(struct ibv_qp *qp,
			  struct usb4_rdma_dv_wqe *sq,
			  struct usb4_rdma_dv_cqe *cq,
			  struct usb4_rdma_dv_doorbell *doorbell,
			  uint32_t generation)
{
	struct usb4_rdma_dv_kick kick = {};
	uint32_t tail;
	int err;

	store_release_u32(&doorbell->producer.generation, generation);
	store_release_u32(&doorbell->producer.cq_head,
			  usb4_rdma_dv_tail_pack(0, generation));
	store_release_u32(&doorbell->producer.sq_tail,
			  usb4_rdma_dv_tail_pack(0, generation));

	memset(&sq[0], 0, sizeof(sq[0]));
	sq[0].opcode = USB4_RDMA_DV_WQE_NOP;
	sq[0].flags = USB4_RDMA_DV_WQE_F_SIGNALED;
	sq[0].wr_id = 0x4b49434b4e4f5031ULL;
	sq[0].generation = generation;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	tail = usb4_rdma_dv_tail_pack(1, generation);
	store_release_u32(&doorbell->producer.sq_tail, tail);
	kick.sq_tail = tail;

	err = kick_queue(qp, &kick);
	if (err) {
		fprintf(stderr, "KICK nop failed: %s (%d)\n", strerror(err),
			err);
		return 1;
	}
	if (expect_cqe("kick_nop", &cq[0], doorbell, 0, generation,
		       sq[0].wr_id, USB4_RDMA_DV_CQE_SUCCESS,
		       USB4_RDMA_DV_WQE_NOP))
		return 1;
	store_release_u32(&doorbell->producer.cq_head,
			  usb4_rdma_dv_tail_pack(1, generation));

	memset(&sq[1], 0, sizeof(sq[1]));
	sq[1].opcode = USB4_RDMA_DV_WQE_NOP;
	sq[1].flags = USB4_RDMA_DV_WQE_F_SIGNALED;
	sq[1].wr_id = 0x4b49434b5354414cULL;
	sq[1].generation = next_generation(generation);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	tail = usb4_rdma_dv_tail_pack(2, generation);
	store_release_u32(&doorbell->producer.sq_tail, tail);
	kick.sq_tail = tail;

	err = kick_queue(qp, &kick);
	if (err) {
		fprintf(stderr, "KICK stale-gen failed: %s (%d)\n",
			strerror(err), err);
		return 1;
	}
	if (expect_cqe("kick_stale_gen", &cq[1], doorbell, 1, generation,
		       sq[1].wr_id, USB4_RDMA_DV_CQE_STALE_GEN,
		       USB4_RDMA_DV_WQE_NOP))
		return 1;
	store_release_u32(&doorbell->producer.cq_head,
			  usb4_rdma_dv_tail_pack(2, generation));
	return 0;
}

static int run_queue_test(struct ibv_context *ctx,
			  const struct usb4_rdma_dv_query_caps_resp *caps)
{
	struct usb4_rdma_dv_queue_create req = {};
	struct usb4_rdma_dv_queue_resp resp = {};
	struct ibv_qp_init_attr qp_attr = {};
	struct ibv_mr *doorbell_mr = NULL;
	struct ibv_mr *cq_mr = NULL;
	struct ibv_mr *sq_mr = NULL;
	struct ibv_qp *qp = NULL;
	struct ibv_cq *cq = NULL;
	struct ibv_pd *pd = NULL;
	size_t doorbell_bytes = caps->doorbell_page_size;
	size_t cq_bytes = (size_t)caps->default_cq_entries * caps->cqe_size;
	size_t sq_bytes = (size_t)caps->default_sq_entries * caps->wqe_size;
	void *doorbell = NULL;
	void *cq_buf = NULL;
	void *sq_buf = NULL;
	int access = IBV_ACCESS_LOCAL_WRITE;
	int ret = 1;
	int err;

	if (caps->abi_version != USB4_RDMA_DV_ABI_VERSION) {
		fprintf(stderr, "queue test skipped: unsupported ABI %u\n",
			caps->abi_version);
		return 2;
	}

	err = alloc_aligned(caps->wqe_size, sq_bytes, &sq_buf);
	if (err) {
		fprintf(stderr, "alloc SQ: %s\n", strerror(err));
		goto out;
	}
	err = alloc_aligned(caps->cqe_size, cq_bytes, &cq_buf);
	if (err) {
		fprintf(stderr, "alloc CQ: %s\n", strerror(err));
		goto out;
	}
	err = alloc_aligned(caps->doorbell_page_size, doorbell_bytes, &doorbell);
	if (err) {
		fprintf(stderr, "alloc doorbell: %s\n", strerror(err));
		goto out;
	}

	pd = ibv_alloc_pd(ctx);
	if (!pd) {
		fprintf(stderr, "ibv_alloc_pd: %s\n", strerror(errno));
		goto out;
	}
	sq_mr = ibv_reg_mr(pd, sq_buf, sq_bytes, access);
	if (!sq_mr) {
		fprintf(stderr, "ibv_reg_mr(SQ): %s\n", strerror(errno));
		goto out;
	}
	cq_mr = ibv_reg_mr(pd, cq_buf, cq_bytes, access);
	if (!cq_mr) {
		fprintf(stderr, "ibv_reg_mr(CQ): %s\n", strerror(errno));
		goto out;
	}
	doorbell_mr = ibv_reg_mr(pd, doorbell, doorbell_bytes, access);
	if (!doorbell_mr) {
		fprintf(stderr, "ibv_reg_mr(doorbell): %s\n", strerror(errno));
		goto out;
	}

	cq = ibv_create_cq(ctx, caps->default_cq_entries, NULL, NULL, 0);
	if (!cq) {
		fprintf(stderr, "ibv_create_cq: %s\n", strerror(errno));
		goto out;
	}

	qp_attr.send_cq = cq;
	qp_attr.recv_cq = cq;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.cap.max_send_wr = caps->default_sq_entries;
	qp_attr.cap.max_recv_wr = 1;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_sge = 1;
	qp = ibv_create_qp(pd, &qp_attr);
	if (!qp) {
		fprintf(stderr, "ibv_create_qp: %s\n", strerror(errno));
		goto out;
	}

	req.abi_version = USB4_RDMA_DV_ABI_VERSION;
	req.sq_addr = (uintptr_t)sq_buf;
	req.cq_addr = (uintptr_t)cq_buf;
	req.doorbell_addr = (uintptr_t)doorbell;
	req.sq_entries = caps->default_sq_entries;
	req.cq_entries = caps->default_cq_entries;
	req.sq_stride = caps->wqe_size;
	req.cq_stride = caps->cqe_size;

	err = create_queue(qp, &req, &resp);
	if (err) {
		fprintf(stderr, "CREATE_QUEUE failed: %s (%d)\n",
			strerror(err), err);
		goto out;
	}

	printf("create_queue qp_num=%u generation=%u sq_entries=%u cq_entries=%u\n",
	       resp.qp_num, resp.generation, req.sq_entries, req.cq_entries);

	err = run_kick_smoke(qp, sq_buf, cq_buf, doorbell, resp.generation);
	if (err)
		goto out;

	err = destroy_queue(qp);
	if (err) {
		fprintf(stderr, "DESTROY_QUEUE failed: %s (%d)\n",
			strerror(err), err);
		goto out;
	}
	printf("destroy_queue ok\n");
	ret = 0;

out:
	if (qp && ibv_destroy_qp(qp))
		fprintf(stderr, "ibv_destroy_qp: %s\n", strerror(errno));
	if (cq && ibv_destroy_cq(cq))
		fprintf(stderr, "ibv_destroy_cq: %s\n", strerror(errno));
	if (doorbell_mr && ibv_dereg_mr(doorbell_mr))
		fprintf(stderr, "ibv_dereg_mr(doorbell): %s\n", strerror(errno));
	if (cq_mr && ibv_dereg_mr(cq_mr))
		fprintf(stderr, "ibv_dereg_mr(CQ): %s\n", strerror(errno));
	if (sq_mr && ibv_dereg_mr(sq_mr))
		fprintf(stderr, "ibv_dereg_mr(SQ): %s\n", strerror(errno));
	if (pd && ibv_dealloc_pd(pd))
		fprintf(stderr, "ibv_dealloc_pd: %s\n", strerror(errno));
	free_aligned(doorbell);
	free_aligned(cq_buf);
	free_aligned(sq_buf);
	return ret;
}

int main(int argc, char **argv)
{
	const char *wanted = "usb4_rdma0";
	struct usb4_rdma_dv_query_caps_resp caps;
	struct ibv_device **list;
	struct ibv_context *ctx;
	struct ibv_device *dev;
	bool queue_test = false;
	int count = 0;
	int ret;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--queue") == 0) {
			queue_test = true;
		} else {
			wanted = argv[i];
		}
	}

	list = ibv_get_device_list(&count);
	if (!list) {
		perror("ibv_get_device_list");
		return 1;
	}

	dev = find_device(list, count, wanted);
	if (!dev) {
		fprintf(stderr, "no USB4 RDMA device found (wanted %s)\n",
			wanted);
		ibv_free_device_list(list);
		return 1;
	}

	ctx = ibv_open_device(dev);
	if (!ctx) {
		fprintf(stderr, "ibv_open_device(%s): %s\n",
			ibv_get_device_name(dev), strerror(errno));
		ibv_free_device_list(list);
		return 1;
	}

	ret = query_caps(ctx, &caps);
	if (ret) {
		fprintf(stderr, "USB4 DV QUERY_CAPS failed on %s: %s (%d)\n",
			ibv_get_device_name(dev), strerror(ret), ret);
		ibv_close_device(ctx);
		ibv_free_device_list(list);
		return 1;
	}

	printf("device=%s abi=%u caps=0x%08x"
	       " send=%u send_imm=%u write=%u write_imm=%u fence=%u"
	       " max_sq=%u default_sq=%u max_cq=%u default_cq=%u"
	       " wqe=%u cqe=%u doorbell_record=%u doorbell_page=%u"
	       " tail_index_bits=%u tail_generation_bits=%u\n",
	       ibv_get_device_name(dev), caps.abi_version, caps.caps,
	       !!(caps.caps & USB4_RDMA_DV_CAP_SEND),
	       !!(caps.caps & USB4_RDMA_DV_CAP_SEND_IMM),
	       !!(caps.caps & USB4_RDMA_DV_CAP_WRITE),
	       !!(caps.caps & USB4_RDMA_DV_CAP_WRITE_IMM),
	       !!(caps.caps & USB4_RDMA_DV_CAP_FENCE),
	       caps.max_sq_entries, caps.default_sq_entries,
	       caps.max_cq_entries, caps.default_cq_entries,
	       caps.wqe_size, caps.cqe_size, caps.doorbell_record_size,
	       caps.doorbell_page_size, caps.tail_index_bits,
	       caps.tail_generation_bits);
	print_doorbell_layout();

	if (queue_test) {
		ret = run_queue_test(ctx, &caps);
		ibv_close_device(ctx);
		ibv_free_device_list(list);
		return ret;
	}

	ibv_close_device(ctx);
	ibv_free_device_list(list);
	return caps.abi_version == USB4_RDMA_DV_ABI_VERSION ? 0 : 2;
}
