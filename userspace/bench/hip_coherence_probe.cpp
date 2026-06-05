// SPDX-License-Identifier: MIT
/*
 * Probe GPU<->CPU visibility for the GDA queue-memory contract.
 *
 * This is intentionally a userspace-only test. It checks that a GPU can publish
 * descriptor-like payload with a system-scope release tail update and that the
 * CPU can consume it with an acquire load. It also checks the reverse CQ-like
 * direction.
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <hip/hip_runtime.h>

#include "usb4_rdma_dv.h"

struct alignas(64) Shared {
	uint32_t gpu_tail;
	uint8_t pad0[60];
	uint32_t cpu_tail;
	uint8_t pad1[60];
	uint32_t gpu_error;
	uint8_t pad2[60];
	uint64_t payload[8];
};

static_assert(alignof(Shared) == 64, "Shared must keep tail words isolated");

struct alignas(4096) DvShared {
	struct usb4_rdma_dv_wqe wqe;
	uint8_t pad0[4096 - USB4_RDMA_DV_WQE_SIZE];
	struct usb4_rdma_dv_doorbell doorbell;
};

static_assert(sizeof(struct usb4_rdma_dv_wqe) == USB4_RDMA_DV_WQE_SIZE,
	      "unexpected DV WQE size");
static_assert(sizeof(struct usb4_rdma_dv_doorbell) ==
		      USB4_RDMA_DV_DOORBELL_RECORD_SIZE,
	      "unexpected DV doorbell size");

static constexpr uint64_t PATTERN_BASE = 0x475055434f484552ULL;
static constexpr uint64_t DV_WR_ID_BASE = 0x445650524f443031ULL;
static constexpr uint32_t MAX_GPU_SPINS = 10000000u;
static constexpr uint32_t DV_GENERATION = 1u;

__device__ static inline void gpu_store_release(uint32_t *ptr, uint32_t value)
{
	__hip_atomic_store(ptr, value, __ATOMIC_RELEASE,
			   __HIP_MEMORY_SCOPE_SYSTEM);
}

__device__ static inline uint32_t gpu_load_acquire(uint32_t *ptr)
{
	return __hip_atomic_load(ptr, __ATOMIC_ACQUIRE,
				 __HIP_MEMORY_SCOPE_SYSTEM);
}

__device__ static inline uint64_t pattern(uint32_t iter, int lane)
{
	return PATTERN_BASE ^ (static_cast<uint64_t>(iter) << 16) ^
	       static_cast<uint64_t>(lane);
}

__device__ static inline uint32_t gpu_tail_pack(uint32_t index,
						uint32_t generation)
{
	return (index & USB4_RDMA_DV_TAIL_INDEX_MASK) |
	       (generation << USB4_RDMA_DV_TAIL_GEN_SHIFT);
}

__global__ void gpu_to_cpu_kernel(Shared *shared, uint32_t iterations,
				  uint32_t max_spins)
{
	if (threadIdx.x || blockIdx.x)
		return;

	for (uint32_t iter = 1; iter <= iterations; iter++) {
		for (int i = 0; i < 8; i++)
			shared->payload[i] = pattern(iter, i);

		gpu_store_release(&shared->gpu_tail, iter);

		uint32_t spins = 0;
		while (gpu_load_acquire(&shared->cpu_tail) != iter) {
			if (++spins == max_spins) {
				gpu_store_release(&shared->gpu_error, iter);
				return;
			}
		}
	}
}

__global__ void cpu_to_gpu_kernel(Shared *shared, uint32_t iterations,
				  uint32_t max_spins)
{
	if (threadIdx.x || blockIdx.x)
		return;

	for (uint32_t iter = 1; iter <= iterations; iter++) {
		uint32_t spins = 0;
		while (gpu_load_acquire(&shared->cpu_tail) != iter) {
			if (++spins == max_spins) {
				gpu_store_release(&shared->gpu_error, iter);
				return;
			}
		}

		for (int i = 0; i < 8; i++) {
			if (shared->payload[i] != pattern(iter, i)) {
				gpu_store_release(&shared->gpu_error, iter);
				return;
			}
		}

		gpu_store_release(&shared->gpu_tail, iter);
	}
}

__global__ void dv_producer_kernel(DvShared *shared, uint32_t iterations,
				   uint32_t max_spins)
{
	if (threadIdx.x || blockIdx.x)
		return;

	gpu_store_release(&shared->doorbell.producer.generation,
			  DV_GENERATION);
	for (uint32_t iter = 1; iter <= iterations; iter++) {
		uint32_t tail = gpu_tail_pack(iter, DV_GENERATION);
		struct usb4_rdma_dv_wqe *wqe = &shared->wqe;

		wqe->opcode = USB4_RDMA_DV_WQE_SEND;
		wqe->flags = USB4_RDMA_DV_WQE_F_SIGNALED |
			     ((iter & 1u) ? USB4_RDMA_DV_WQE_F_FENCE : 0);
		wqe->length = 4096u + (iter & 0xffu);
		wqe->wr_id = DV_WR_ID_BASE + iter;
		wqe->local_addr = 0x100000000ULL + (static_cast<uint64_t>(iter)
						    << 12);
		wqe->lkey = 0xabc00000u ^ iter;
		wqe->rkey = 0xdef00000u ^ (iter << 1);
		wqe->remote_addr = 0x200000000ULL +
				   (static_cast<uint64_t>(iter) << 13);
		wqe->imm_data = 0x12340000u ^ iter;
		wqe->generation = DV_GENERATION;
		wqe->reserved1[0] = pattern(iter, 0);
		wqe->reserved1[1] = pattern(iter, 1);

		gpu_store_release(&shared->doorbell.producer.sq_tail, tail);

		uint32_t spins = 0;
		while (gpu_load_acquire(&shared->doorbell.consumer.sq_head) !=
		       tail) {
			if (++spins == max_spins) {
				gpu_store_release(
					&shared->doorbell.consumer.qp_state,
					iter);
				return;
			}
		}
	}
}

static uint64_t host_pattern(uint32_t iter, int lane)
{
	return PATTERN_BASE ^ (static_cast<uint64_t>(iter) << 16) ^
	       static_cast<uint64_t>(lane);
}

static uint32_t host_load_acquire(uint32_t *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static void host_store_release(uint32_t *ptr, uint32_t value)
{
	__atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static bool timed_out(std::chrono::steady_clock::time_point start,
		      int timeout_ms)
{
	auto elapsed = std::chrono::steady_clock::now() - start;
	return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
		       .count() > timeout_ms;
}

static hipError_t alloc_shared(const char *kind, Shared **out)
{
	unsigned int flags = hipHostMallocMapped;
	void *ptr = nullptr;
	hipError_t ret;

	if (!strcmp(kind, "managed")) {
		ret = hipMallocManaged(&ptr, sizeof(Shared));
	} else if (!strcmp(kind, "host")) {
		ret = hipHostMalloc(&ptr, sizeof(Shared), flags);
	} else if (!strcmp(kind, "host-coherent")) {
		flags |= hipHostMallocCoherent;
		ret = hipHostMalloc(&ptr, sizeof(Shared), flags);
	} else if (!strcmp(kind, "host-noncoherent")) {
		flags |= hipHostMallocNonCoherent;
		ret = hipHostMalloc(&ptr, sizeof(Shared), flags);
	} else if (!strcmp(kind, "host-uncached")) {
		flags |= hipHostMallocUncached;
		ret = hipHostMalloc(&ptr, sizeof(Shared), flags);
	} else {
		fprintf(stderr,
			"usage: hip_coherence_probe managed|host|host-coherent|host-noncoherent|host-uncached [iterations] [gpu-to-cpu|cpu-to-gpu|both]\n");
		return hipErrorInvalidValue;
	}

	if (ret == hipSuccess) {
		memset(ptr, 0, sizeof(Shared));
		*out = static_cast<Shared *>(ptr);
	}
	return ret;
}

static hipError_t alloc_dv_shared(const char *kind, DvShared **out)
{
	unsigned int flags = hipHostMallocMapped;
	void *ptr = nullptr;
	hipError_t ret;

	if (!strcmp(kind, "managed")) {
		ret = hipMallocManaged(&ptr, sizeof(DvShared));
	} else if (!strcmp(kind, "host")) {
		ret = hipHostMalloc(&ptr, sizeof(DvShared), flags);
	} else if (!strcmp(kind, "host-coherent")) {
		flags |= hipHostMallocCoherent;
		ret = hipHostMalloc(&ptr, sizeof(DvShared), flags);
	} else if (!strcmp(kind, "host-noncoherent")) {
		flags |= hipHostMallocNonCoherent;
		ret = hipHostMalloc(&ptr, sizeof(DvShared), flags);
	} else if (!strcmp(kind, "host-uncached")) {
		flags |= hipHostMallocUncached;
		ret = hipHostMalloc(&ptr, sizeof(DvShared), flags);
	} else {
		fprintf(stderr,
			"usage: hip_coherence_probe managed|host|host-coherent|host-noncoherent|host-uncached [iterations] [gpu-to-cpu|cpu-to-gpu|both|dv-producer]\n");
		return hipErrorInvalidValue;
	}

	if (ret == hipSuccess) {
		memset(ptr, 0, sizeof(DvShared));
		*out = static_cast<DvShared *>(ptr);
	}
	return ret;
}

static void free_shared(const char *kind, Shared *shared)
{
	if (!strcmp(kind, "managed"))
		(void)hipFree(shared);
	else
		(void)hipHostFree(shared);
}

static void free_dv_shared(const char *kind, DvShared *shared)
{
	if (!strcmp(kind, "managed"))
		(void)hipFree(shared);
	else
		(void)hipHostFree(shared);
}

static int run_gpu_to_cpu(Shared *shared, uint32_t iterations)
{
	int mismatches = 0;
	const int timeout_ms = 5000;

	memset(shared, 0, sizeof(*shared));
	hipLaunchKernelGGL(gpu_to_cpu_kernel, dim3(1), dim3(1), 0, 0, shared,
			   iterations, MAX_GPU_SPINS);
	hipError_t ret = hipGetLastError();
	if (ret != hipSuccess) {
		fprintf(stderr, "launch gpu-to-cpu failed: %s\n",
			hipGetErrorString(ret));
		return 1;
	}

	for (uint32_t iter = 1; iter <= iterations; iter++) {
		auto start = std::chrono::steady_clock::now();
		while (host_load_acquire(&shared->gpu_tail) != iter) {
			if (host_load_acquire(&shared->gpu_error)) {
				(void)hipDeviceSynchronize();
				printf("direction=gpu-to-cpu iterations=%u status=FAIL mismatches=%d gpu_error=%u\n",
				       iterations, mismatches,
				       host_load_acquire(&shared->gpu_error));
				return 1;
			}
			if (timed_out(start, timeout_ms)) {
				fprintf(stderr,
					"gpu-to-cpu timeout waiting for iter=%u tail=%u gpu_error=%u\n",
					iter, host_load_acquire(&shared->gpu_tail),
					host_load_acquire(&shared->gpu_error));
				(void)hipDeviceSynchronize();
				return 1;
			}
		}

		for (int i = 0; i < 8; i++) {
			if (shared->payload[i] != host_pattern(iter, i))
				mismatches++;
		}
		host_store_release(&shared->cpu_tail, iter);
	}

	ret = hipDeviceSynchronize();
	printf("direction=gpu-to-cpu iterations=%u status=%s mismatches=%d gpu_error=%u\n",
	       iterations, ret == hipSuccess && !mismatches ? "OK" : "FAIL",
	       mismatches, host_load_acquire(&shared->gpu_error));
	return ret == hipSuccess && !mismatches &&
		       !host_load_acquire(&shared->gpu_error)
	       ? 0
	       : 1;
}

static int run_cpu_to_gpu(Shared *shared, uint32_t iterations)
{
	const int timeout_ms = 5000;

	memset(shared, 0, sizeof(*shared));
	hipLaunchKernelGGL(cpu_to_gpu_kernel, dim3(1), dim3(1), 0, 0, shared,
			   iterations, MAX_GPU_SPINS);
	hipError_t ret = hipGetLastError();
	if (ret != hipSuccess) {
		fprintf(stderr, "launch cpu-to-gpu failed: %s\n",
			hipGetErrorString(ret));
		return 1;
	}

	for (uint32_t iter = 1; iter <= iterations; iter++) {
		for (int i = 0; i < 8; i++)
			shared->payload[i] = host_pattern(iter, i);

		host_store_release(&shared->cpu_tail, iter);

		auto start = std::chrono::steady_clock::now();
		while (host_load_acquire(&shared->gpu_tail) != iter) {
			if (host_load_acquire(&shared->gpu_error)) {
				(void)hipDeviceSynchronize();
				printf("direction=cpu-to-gpu iterations=%u status=FAIL gpu_error=%u\n",
				       iterations,
				       host_load_acquire(&shared->gpu_error));
				return 1;
			}
			if (timed_out(start, timeout_ms)) {
				fprintf(stderr,
					"cpu-to-gpu timeout waiting for iter=%u tail=%u gpu_error=%u\n",
					iter, host_load_acquire(&shared->gpu_tail),
					host_load_acquire(&shared->gpu_error));
				(void)hipDeviceSynchronize();
				return 1;
			}
		}
	}

	ret = hipDeviceSynchronize();
	printf("direction=cpu-to-gpu iterations=%u status=%s gpu_error=%u\n",
	       iterations, ret == hipSuccess ? "OK" : "FAIL",
	       host_load_acquire(&shared->gpu_error));
	return ret == hipSuccess && !host_load_acquire(&shared->gpu_error) ? 0 : 1;
}

static int validate_dv_wqe(const struct usb4_rdma_dv_wqe &wqe, uint32_t iter)
{
	uint16_t flags = USB4_RDMA_DV_WQE_F_SIGNALED |
			 ((iter & 1u) ? USB4_RDMA_DV_WQE_F_FENCE : 0);

	if (wqe.opcode != USB4_RDMA_DV_WQE_SEND || wqe.flags != flags ||
	    wqe.length != 4096u + (iter & 0xffu) ||
	    wqe.wr_id != DV_WR_ID_BASE + iter ||
	    wqe.local_addr != 0x100000000ULL +
				      (static_cast<uint64_t>(iter) << 12) ||
	    wqe.lkey != (0xabc00000u ^ iter) ||
	    wqe.rkey != (0xdef00000u ^ (iter << 1)) ||
	    wqe.remote_addr != 0x200000000ULL +
				       (static_cast<uint64_t>(iter) << 13) ||
	    wqe.imm_data != (0x12340000u ^ iter) ||
	    wqe.generation != DV_GENERATION ||
	    wqe.reserved1[0] != host_pattern(iter, 0) ||
	    wqe.reserved1[1] != host_pattern(iter, 1))
		return -1;
	return 0;
}

static int run_dv_producer(DvShared *shared, uint32_t iterations)
{
	const int timeout_ms = 5000;
	int mismatches = 0;

	memset(shared, 0, sizeof(*shared));
	host_store_release(&shared->doorbell.consumer.generation, DV_GENERATION);

	hipLaunchKernelGGL(dv_producer_kernel, dim3(1), dim3(1), 0, 0,
			   shared, iterations, MAX_GPU_SPINS);
	hipError_t ret = hipGetLastError();
	if (ret != hipSuccess) {
		fprintf(stderr, "launch dv-producer failed: %s\n",
			hipGetErrorString(ret));
		return 1;
	}

	for (uint32_t iter = 1; iter <= iterations; iter++) {
		uint32_t want_tail =
			usb4_rdma_dv_tail_pack(iter, DV_GENERATION);
		auto start = std::chrono::steady_clock::now();

		while (host_load_acquire(&shared->doorbell.producer.sq_tail) !=
		       want_tail) {
			if (host_load_acquire(
				    &shared->doorbell.consumer.qp_state)) {
				(void)hipDeviceSynchronize();
				printf("direction=dv-producer iterations=%u status=FAIL mismatches=%d gpu_error=%u\n",
				       iterations, mismatches,
				       host_load_acquire(
					       &shared->doorbell.consumer
							.qp_state));
				return 1;
			}
			if (timed_out(start, timeout_ms)) {
				fprintf(stderr,
					"dv-producer timeout iter=%u sq_tail=%u gpu_error=%u\n",
					iter,
					host_load_acquire(
						&shared->doorbell.producer
							 .sq_tail),
					host_load_acquire(
						&shared->doorbell.consumer
							 .qp_state));
				(void)hipDeviceSynchronize();
				return 1;
			}
		}

		if (validate_dv_wqe(shared->wqe, iter))
			mismatches++;
		host_store_release(&shared->doorbell.consumer.sq_head,
				   want_tail);
	}

	ret = hipDeviceSynchronize();
	printf("direction=dv-producer iterations=%u status=%s mismatches=%d sq_tail=%u sq_head=%u producer_generation=%u\n",
	       iterations, ret == hipSuccess && !mismatches ? "OK" : "FAIL",
	       mismatches,
	       host_load_acquire(&shared->doorbell.producer.sq_tail),
	       host_load_acquire(&shared->doorbell.consumer.sq_head),
	       host_load_acquire(&shared->doorbell.producer.generation));
	return ret == hipSuccess && !mismatches ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *kind = argc > 1 ? argv[1] : "host-coherent";
	uint32_t iterations = argc > 2 ? strtoul(argv[2], nullptr, 0) : 10000;
	std::string direction = argc > 3 ? argv[3] : "both";
	Shared *shared = nullptr;
	int rc = 0;

	hipError_t ret = hipSetDevice(0);
	if (ret != hipSuccess) {
		fprintf(stderr, "hipSetDevice failed: %s\n",
			hipGetErrorString(ret));
		return 1;
	}

	if (direction == "dv-producer") {
		DvShared *dv_shared = nullptr;

		ret = alloc_dv_shared(kind, &dv_shared);
		if (ret != hipSuccess) {
			fprintf(stderr, "allocation kind=%s failed: %s\n",
				kind, hipGetErrorString(ret));
			return 1;
		}
		printf("kind=%s ptr=%p bytes=%zu direction=dv-producer\n",
		       kind, dv_shared, sizeof(*dv_shared));
		rc = run_dv_producer(dv_shared, iterations);
		free_dv_shared(kind, dv_shared);
		return rc;
	}

	ret = alloc_shared(kind, &shared);
	if (ret != hipSuccess) {
		fprintf(stderr, "allocation kind=%s failed: %s\n", kind,
			hipGetErrorString(ret));
		return 1;
	}

	printf("kind=%s ptr=%p bytes=%zu\n", kind, shared, sizeof(*shared));
	if (direction == "gpu-to-cpu" || direction == "both")
		rc |= run_gpu_to_cpu(shared, iterations);
	if (direction == "cpu-to-gpu" || direction == "both")
		rc |= run_cpu_to_gpu(shared, iterations);

	free_shared(kind, shared);
	return rc;
}
