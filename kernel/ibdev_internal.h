/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared internal declarations for the tbv ibverbs provider.
 *
 * ibdev.c was historically a single ~10k-line translation unit.  These
 * macros, structures, and cross-unit helper prototypes are factored out here
 * so the verb entry points can live in focused units (ibdev_cq.c,
 * ibdev_mr.c, ibdev_qp.c, ibdev_native.c, ibdev_apple.c) while still sharing
 * the provider's private types and helpers.  Definitions remain in ibdev.c;
 * this header only declares what crosses unit boundaries.
 */
#ifndef TBV_IBDEV_INTERNAL_H
#define TBV_IBDEV_INTERNAL_H

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>

#include "../proto/native_data.h"
#include "../proto/reliability.h"
#include "tbv.h"

/* ---- provider tunables and limits ---- */
#define TBV_IBDEV_ABI_VERSION 1
#define TBV_IBDEV_PORTS 1
#define TBV_IBDEV_MAX_QP 256
#define TBV_IBDEV_MAX_QP_WR 1024
#define TBV_IBDEV_MAX_CQ 256
#define TBV_IBDEV_MAX_CQE 4096
#define TBV_IBDEV_MAX_MR 1024
#define TBV_IBDEV_MAX_SGE 4
#define TBV_IBDEV_MAX_READ_CTX 128
#define TBV_IBDEV_QPN_MIN 0x900
#define TBV_IBDEV_QPN_MAX 0x00ffffff
#define TBV_APPLE_PRIMARY_QPN TBV_IBDEV_QPN_MIN
#define TBV_IBDEV_PAGE_SIZE_CAP (SZ_4K | SZ_2M | SZ_1G)
/*
 * Memory keys (rkey/lkey) are drawn from a CSPRNG so that a remote peer cannot
 * guess a valid rkey and perform arbitrary remote DMA.  Each MR consumes two
 * keys (a separate rkey and lkey), both unique within the device key map; this
 * bounds how many random draws we attempt before reporting key exhaustion.
 */
#define TBV_MR_KEY_MAX_ATTEMPTS 16
#define TBV_PSN_MASK 0x00ffffffu
/*
 * Native SEND receives can observe future PSNs when Thunderbolt paths deliver
 * fragments out of order under load.  The provider advertises
 * TBV_IBDEV_MAX_QP_WR outstanding WRs, so the receive-side reorder window must
 * cover that contract instead of failing legal traffic at an arbitrary smaller
 * depth.
 */
#define TBV_RX_REORDER_MAX_MESSAGES TBV_IBDEV_MAX_QP_WR
#define TBV_RX_REORDER_MAX_BYTES (64u * 1024u * 1024u)
#define TBV_RX_REORDER_MAX_FRAGS TBV_NATIVE_DATA_MAX_FRAGS
#define TBV_ACK_HISTORY_SIZE TBV_IBDEV_MAX_QP_WR
#define TBV_IBDEV_GID_TBL_LEN 8
#define TBV_APPLE_PENDING_RX_DEFAULT_SLOTS 4096
#define TBV_APPLE_PENDING_RX_MAX_SLOTS 16384
#define TBV_APPLE_PENDING_RX_TOTAL_BYTES_DEFAULT (64u * 1024u * 1024u)
#define TBV_APPLE_RAW_SLOT_USER_SIZE 256
#define TBV_APPLE_RAW_SPLIT_USER_SIZE 16
#define TBV_APPLE_RAW_TAIL_USER_SIZE 240
#define TBV_APPLE_RAW_DESCS_PER_CHUNK 17
#define TBV_QP_TIMEOUT_DEFAULT_MS 5000
#define TBV_QP_TIMEOUT_WORK_INTERVAL_MS 1000
#define TBV_QP_DESTROY_TIMEOUT_MS 5000
#define TBV_SEND_MAX_RETRIES 7
#define TBV_SEND_RNR_RETRIES_INFINITE ((u8)~0u)
#define TBV_READ_RESP_RETRY_MS 100
#define TBV_READ_RESP_MAX_RETRIES 3
#define TBV_GSI_QPN 1
#define TBV_GSI_MAD_META_SIZE 40
#define TBV_GSI_MAD_META_SGID_OFF 0
#define TBV_GSI_MAD_META_DGID_OFF 16
#define TBV_GSI_MAD_META_PKEY_OFF 32
#define TBV_MR_KEY_ALLOC_MAX_ATTEMPTS 32

/* ---- provider private structures ---- */
struct tbv_ucontext {
	struct ib_ucontext base;
	struct tbv_state *owner;
};

struct tbv_pd {
	struct ib_pd base;
	struct tbv_state *owner;
};

struct tbv_cq {
	struct ib_cq base;
	struct tbv_state *owner;
	spinlock_t lock; /* protects entries, head, tail, count */
	struct ib_wc *entries;
	u32 cqe;
	u32 head;
	u32 tail;
	u32 count;
	bool notify_armed;
	bool overflowed;
};

struct tbv_recv_wqe {
	u64 wr_id;
	struct ib_cqe *wr_cqe;
	u64 addr;
	u32 length;
	u32 lkey;
	bool kernel_cqe;
};

struct tbv_send_segment {
	struct tbv_mr *mr;
	u64 addr;
	u32 length;
};

struct tbv_read_segment {
	struct tbv_mr *mr;
	u64 addr;
	u32 length;
};

struct tbv_rx_message {
	struct tbv_recv_wqe wqe;
	unsigned long started_jiffies;
	u32 src_qp;
	u32 psn;
	u32 total_len;
	u32 imm_data;
	u32 received;
	u32 delivered;
	u16 frag_count;
	u16 frags_received;
	DECLARE_BITMAP(frag_seen, TBV_RX_REORDER_MAX_FRAGS);
	u32 first_rail_id;
	u32 last_rail_id;
	u64 first_route;
	u64 last_route;
	u32 first_path_id;
	u32 last_path_id;
	u32 last_offset;
	u32 last_len;
	int status;
	bool active;
	bool with_imm;
	bool solicited;
};

struct tbv_rx_write {
	struct tbv_recv_wqe imm_wqe;
	unsigned long started_jiffies;
	u64 remote_addr;
	u32 src_qp;
	u32 psn;
	u32 rkey;
	u32 imm_data;
	u32 received;
	bool active;
	bool with_imm;
	bool solicited;
};

struct tbv_rx_reorder_frag {
	struct list_head node;
	u32 offset;
	u32 len;
	u8 data[];
};

enum tbv_rx_reorder_kind {
	TBV_RX_REORDER_SEND,
	TBV_RX_REORDER_WRITE,
	TBV_RX_REORDER_READ_REQ,
};

struct tbv_rx_reorder_msg {
	struct list_head node;
	struct list_head frags;
	unsigned long first_jiffies;
	enum tbv_rx_reorder_kind kind;
	u64 remote_addr;
	u32 src_qp;
	u32 psn;
	u32 total_len;
	u32 imm_data;
	u32 rkey;
	u32 received;
	u32 buffered_bytes;
	u16 frag_count;
	u16 frags_received;
	DECLARE_BITMAP(frag_seen, TBV_RX_REORDER_MAX_FRAGS);
	bool complete;
	bool with_imm;
	bool solicited;
};

struct tbv_apple_pending_rx {
	void *buf;
	u32 capacity;
	u32 delivered;
	int status;
	bool active;
	bool ready;
};

struct tbv_ack_history_entry {
	u32 psn;
	int status;
	bool valid;
};

enum tbv_send_post_reason {
	TBV_SEND_POST_INITIAL,
	TBV_SEND_POST_RETRY_TIMEOUT,
	TBV_SEND_POST_RETRY_RNR,
};

struct tbv_send_ctx;

struct tbv_apple_sq_entry {
	struct list_head node;
	struct tbv_send_ctx *send;
	void *payload;
	u32 length;
};

struct tbv_qp {
	struct ib_qp base;
	struct tbv_state *owner;
	enum tbv_backend_type backend;
	/*
	 * QP rail invariant: every QP is bound to exactly one rail at
	 * create time and unbound at destroy time. Native non-GSI QPs may be
	 * assigned to any ready rail for the same peer so multi-QP workloads
	 * can use all cables while preserving per-QP FIFO ordering.
	 */
	struct tbv_rail *rail;
	/*
	 * Lock nesting order for peer/QP state:
	 * peer->control_lock -> owner->lock -> lock.
	 */
	spinlock_t lock;
	struct mutex rx_lock; /* serializes RX datapath */
	wait_queue_head_t credit_wait;
	wait_queue_head_t apple_tx_wait;
	wait_queue_head_t refs_wait;
	refcount_t refs;
	struct completion refs_zero;
	struct list_head pending_sends;
	struct list_head pending_reads;
	struct list_head pending_read_resps;
	struct list_head apple_sq;
	struct work_struct apple_sq_work;
	struct work_struct error_work;
	struct delayed_work timeout_work;
	struct ib_qp_init_attr init_attr;
	struct ib_qp_attr attr;
	struct tbv_recv_wqe *recvq;
	enum ib_qp_state state;
	enum ib_qp_type type;
	u32 recvq_size;
	u32 recv_head;
	u32 recv_tail;
	u32 recv_count;
	u32 recv_credits_advertised;
	u32 remote_recv_credits;
	u32 early_remote_recv_credit_src_qp;
	u32 sendq_count;
	atomic_t apple_tx_inflight;
	atomic_t apple_tx_inflight_frames;
	u32 send_psn;
	u32 rx_expected_psn;
	u32 rx_rnr_psn;
	u32 rx_rnr_src_qp;
	u32 rx_rnr_frag_offset;
	u64 rx_rnr_remote_addr;
	u64 peer_session_id;
	struct tbv_rx_message rx_msg;
	struct tbv_rx_write rx_write;
	struct list_head rx_reorder;
	struct tbv_ack_history_entry ack_history[TBV_ACK_HISTORY_SIZE];
	u32 rx_reorder_count;
	u32 rx_reorder_bytes;
	struct tbv_apple_pending_rx *apple_pending;
	u32 apple_pending_slot_count;
	u32 apple_pending_head;
	u32 apple_pending_tail;
	u32 apple_pending_ready_count;
	u32 apple_pending_bytes;
	int apple_pending_active;
	u32 apple_sq_outstanding;
	bool qpn_allocated;
	bool rail_binding_counted;
	bool dest_qp_known;
	bool ack_timeout_set;
	bool early_remote_recv_credit_src_known;
	bool rx_rnr_active;
	bool closing;
	bool timeout_work_armed;
	bool apple_tunnel_active;
	/*
	 * Apple FA57 frames carry no message sequence. When a message-start
	 * frame is dropped, the only safe resync point is the next EOF=3
	 * boundary; until then incoming frames belong to the truncated
	 * message and must not seed a new reassembly. Protected by rx_lock.
	 */
	bool apple_rx_discard;
};

struct tbv_mr {
	struct ib_mr base;
	struct tbv_state *owner;
	struct ib_umem *umem;
	struct ib_umem_dmabuf *umem_dmabuf;
	refcount_t refs;
	struct work_struct free_work;
	u64 start;
	u64 length;
	u64 virt_addr;
	int access;
	u32 peer_id;
	bool closing;
	bool dma_mr;
	bool dmabuf_mr;
	bool dmabuf_dynamic;
};

struct tbv_ah {
	struct ib_ah base;
	struct rdma_ah_attr attr;
};

struct tbv_ibdev {
	struct ib_device base;
	struct tbv_state *state;
	enum tbv_backend_type backend;
	struct net_device *netdev;
	struct tbv_qp *gsi_qp;
	/*
	 * Per-rail device identity. Apple and GSI QPs stay on this rail.
	 * Native data QPs created on this device may be assigned to sibling
	 * rails for the same peer by tbv_select_qp_rail_locked().
	 */
	struct tbv_rail *rail;
};

struct tbv_send_ctx {
	struct list_head node;
	struct list_head retry_node;
	struct tbv_qp *tqp;
	refcount_t refs;
	spinlock_t lock; /* protects completion state */
	unsigned long queued_jiffies;
	unsigned long first_queued_jiffies;
	u64 wr_id;
	struct tbv_send_segment segs[TBV_IBDEV_MAX_SGE];
	int nsegs;
	u64 remote_addr;
	u32 total_len;
	u32 rkey;
	u32 imm_data;
	u32 psn;
	enum tbv_native_data_op opcode;
	enum ib_wc_opcode wc_opcode;
	atomic_t apple_pending;
	atomic_t tx_pending;
	u8 retries;
	u8 max_retries;
	u8 rnr_retries;
	u8 max_rnr_retries;
	enum tbv_send_post_reason retry_reason;
	int completion_status;
	bool signaled;
	bool completed;
	bool ready;
	bool pending;
	bool retryable;
	bool retrying;
	bool rnr_waiting;
	bool recv_credit_required;
	bool solicited;
	bool sq_counted;
	bool apple_window_acquired;
	bool apple_window_wr_acquired;
	bool apple_sq_counted;
	u32 apple_window_frames;
	struct delayed_work apple_complete_work;
	int apple_complete_status;
};

struct tbv_read_ctx {
	struct list_head node;
	struct tbv_qp *tqp;
	refcount_t refs;
	spinlock_t lock; /* protects completion flags */
	struct mutex data_lock; /* serializes data assembly */
	unsigned long queued_jiffies;
	u64 wr_id;
	u32 psn;
	u32 total_len;
	u32 received;
	u32 resp_buffered_bytes;
	int nsegs;
	int completion_status;
	bool signaled;
	bool completed;
	bool ready;
	bool sq_counted;
	struct list_head resp_frags;
	struct tbv_read_segment segs[TBV_IBDEV_MAX_SGE];
};

struct tbv_read_resp_ctx {
	struct list_head node;
	struct list_head retry_node;
	struct tbv_qp *tqp;
	struct tbv_mr *mr;
	struct tbv_path *rx_path;
	refcount_t refs;
	/* Stable RDMA_READ payload snapshot; retries must not re-read the MR. */
	void *data;
	u32 data_len;
	unsigned long queued_jiffies;
	u8 retries;
	bool response_sent;
	bool retrying;
	bool closing;
	struct tbv_native_data_header req;
};

struct tbv_read_req_work {
	struct work_struct work;
	struct tbv_state *state;
	struct tbv_qp *tqp;
	struct tbv_path *rx_path;
	struct tbv_native_data_header hdr;
};

struct tbv_send_page_stream {
	struct tbv_send_segment segs[TBV_IBDEV_MAX_SGE];
	struct tbv_send_ctx *send;
	refcount_t refs;
	u32 offset;
	u32 total_len;
	u32 max_chunk;
	int nsegs;
};

struct tbv_apple_send_fill {
	const void *payload;
	u32 payload_len;
	u32 crc;
	bool append_crc;
};

struct tbv_gsi_send_ctx {
	struct tbv_qp *tqp;
	struct ib_cqe *wr_cqe;
};

/* ---- cross-unit helper prototypes (defined in ibdev.c) ---- */
struct tbv_state *tbv_ibdev_state(struct ib_device *ibdev);
u32 tbv_ibdev_peer_id(struct ib_device *ibdev);
void tbv_mr_free_work(struct work_struct *work);
void tbv_mr_put(struct tbv_mr *mr);


/* ---- QP unit cross-references ----
 * QP create/destroy/modify/query live in ibdev_qp.c; the lifecycle and
 * state-machine helpers they share with the data path are defined in ibdev.c.
 */
extern struct ida tbv_qpn_ida;
extern uint apple_rx_pending_slots;
extern uint apple_rx_pending_bytes;
extern uint apple_rx_pending_total_bytes;

int tbv_alloc_qpn(const struct tbv_state *state,
			 enum tbv_backend_type backend);
void tbv_apple_sq_work(struct work_struct *work);
bool tbv_backend_is_apple(enum tbv_backend_type backend);
void tbv_cancel_read_ctx_packets(struct tbv_read_ctx *read);
void tbv_cancel_send_ctx_packets(struct tbv_send_ctx *send);
void tbv_free_qpn(enum tbv_backend_type backend, u32 qpn);
enum tbv_backend_type tbv_ibdev_backend(struct ib_device *ibdev);
void tbv_qp_advertise_recv_credits(struct tbv_qp *tqp);
void tbv_qp_begin_close(struct tbv_qp *tqp);
void tbv_qp_cancel_read_resps(struct tbv_qp *tqp, struct list_head *flush);
int tbv_qp_ensure_apple_tunnel(struct tbv_qp *tqp, bool *acquired);
void tbv_qp_error_work(struct work_struct *work);
void tbv_qp_flush_active_rx(struct tbv_qp *tqp);
void tbv_qp_flush_apple_pending(struct tbv_qp *tqp);
void tbv_qp_flush_apple_sq(struct tbv_qp *tqp);
void tbv_qp_flush_error(struct tbv_qp *tqp);
void tbv_qp_flush_reads(struct tbv_qp *tqp, struct list_head *flush);
void tbv_qp_flush_reorder(struct tbv_qp *tqp);
void tbv_qp_flush_sends(struct tbv_qp *tqp, struct list_head *flush);
void tbv_qp_put(struct tbv_qp *tqp);
void tbv_qp_release_apple_tunnel(struct tbv_qp *tqp);
bool tbv_qp_state_uses_transport(enum ib_qp_state state);
void tbv_qp_timeout_work(struct work_struct *work);
void tbv_qp_unbind_rail(struct tbv_qp *tqp);
bool tbv_qp_uses_apple_transport(const struct tbv_qp *tqp);
bool tbv_read_complete(struct tbv_read_ctx *read, int status);
void tbv_read_ctx_put(struct tbv_read_ctx *read);
void tbv_read_resp_ctx_put(struct tbv_read_resp_ctx *ctx);
struct tbv_rail *tbv_select_qp_rail_locked(struct tbv_ibdev *dev,
					  enum tbv_backend_type backend,
					  bool gsi, bool *counted);
bool tbv_send_complete(struct tbv_send_ctx *send, int status);
void tbv_send_ctx_put(struct tbv_send_ctx *send);
struct tbv_ibdev *tbv_to_ibdev(struct ib_device *ibdev);
int tbv_validate_modify_qp_locked(struct tbv_qp *tqp,
					 struct ib_qp_attr *attr, int attr_mask,
					 enum ib_qp_state *cur_state,
					 enum ib_qp_state *next_state);

/* ---- post_send/post_recv unit cross-references ----
 * tbv_post_send/tbv_post_recv live in ibdev_qp.c; the data-path build and
 * receive-queue helpers they call are defined in ibdev.c.
 */
int tbv_post_send_one(struct tbv_qp *tqp, const struct ib_send_wr *wr);
bool tbv_qp_allows_post(struct tbv_qp *tqp);
void tbv_recv_wqe_set_wr(struct tbv_qp *tqp, struct tbv_recv_wqe *wqe,
			 const struct ib_recv_wr *wr);
bool tbv_qp_accepts_kernel_dma_lkey(const struct tbv_qp *tqp, u32 lkey);
struct tbv_mr *tbv_mr_get(struct tbv_state *state, u32 key, u32 peer_id);
u32 tbv_qp_peer_id(const struct tbv_qp *tqp);
void tbv_apple_rx_drain_pending_locked(struct tbv_state *state,
				       struct tbv_qp *tqp);
void tbv_rx_drain_reorder_locked(struct tbv_state *state, struct tbv_qp *tqp,
				 struct tbv_path *rx_path);

enum tbv_rx_endpoint_status {
	TBV_RX_ENDPOINT_OK,
	TBV_RX_ENDPOINT_UNCONNECTED,
	TBV_RX_ENDPOINT_BAD_PEER,
	TBV_RX_ENDPOINT_QP_ERROR,
};

/* ---- native/Apple RX unit cross-references ----
 * The RX frame dispatchers live in ibdev_native.c / ibdev_apple.c; the
 * receive-path helpers they call are defined in ibdev.c.
 */
void tbv_count_rx_read_ack(struct tbv_state *state, u32 status);
void tbv_note_matched_send_ack(struct tbv_state *state,
			       const struct tbv_native_data_header *hdr,
			       const struct tbv_send_ctx *send);
bool tbv_qp_ack_is_late_duplicate(struct tbv_qp *tqp, u32 psn);
bool tbv_qp_complete_send_ordered(struct tbv_qp *tqp, u32 psn, int status,
				  struct list_head *complete,
				  struct tbv_send_ctx **matched_out);
struct tbv_qp *tbv_qp_get_by_num(struct tbv_state *state, u32 qpn);
bool tbv_qp_mark_error(struct tbv_qp *tqp);
bool tbv_qp_note_rnr_ack(struct tbv_qp *tqp, u32 psn, struct list_head *complete,
			 struct tbv_send_ctx **matched_out);
bool tbv_qp_retry_read_resp(struct tbv_qp *tqp, u32 psn);
void tbv_qp_schedule_timeout_now(struct tbv_qp *tqp);
struct tbv_read_resp_ctx *tbv_qp_take_read_resp(struct tbv_qp *tqp, u32 psn);
void tbv_rx_bad_header_note(struct tbv_state *state, struct tbv_path *rx_path,
			    atomic64_t *reason_counter, const char *reason,
			    const struct tbv_native_data_header *hdr,
			    u32 frame_len, int ret);
void tbv_rx_bad_header_parse_note(struct tbv_state *state,
				  struct tbv_path *rx_path, const void *data,
				  u32 len, int ret);
void tbv_rx_handle_mad(struct tbv_state *state, struct tbv_path *rx_path,
		       const struct tbv_native_data_header *hdr,
		       const void *payload);
void tbv_rx_handle_rdma_read_req(struct tbv_state *state, struct tbv_qp *tqp,
				 const struct tbv_native_data_header *hdr,
				 struct tbv_path *rx_path);
void tbv_rx_handle_rdma_read_resp(struct tbv_state *state, struct tbv_qp *tqp,
				  const struct tbv_native_data_header *hdr,
				  const void *payload, struct tbv_path *rx_path);
void tbv_rx_handle_rdma_write_fragment(struct tbv_state *state,
				       struct tbv_qp *tqp,
				       const struct tbv_native_data_header *hdr,
				       const void *payload,
				       struct tbv_path *rx_path);
void tbv_rx_handle_send_fragment(struct tbv_state *state, struct tbv_qp *tqp,
				 const struct tbv_native_data_header *hdr,
				 const void *payload, struct tbv_path *rx_path);
enum tbv_rx_endpoint_status
tbv_qp_accept_recv_credit(struct tbv_qp *tqp,
			  const struct tbv_native_data_header *hdr);
enum tbv_rx_endpoint_status
tbv_qp_validate_native_endpoint(struct tbv_qp *tqp,
				const struct tbv_native_data_header *hdr);
void tbv_apple_pending_finish_locked(struct tbv_qp *tqp);
u32 tbv_apple_qpn_from_path(const struct tbv_path *path);
int tbv_apple_rx_copy_frame_to_buf(struct tbv_qp *tqp,
				   struct tbv_apple_pending_rx *p,
				   const void *payload, u32 len, u8 eof,
				   u32 *out_user_len);
bool tbv_apple_rx_trace_take(void);
struct tbv_apple_pending_rx *
tbv_apple_pending_active_locked(struct tbv_state *state, struct tbv_qp *tqp);

#endif /* TBV_IBDEV_INTERNAL_H */
