// SPDX-License-Identifier: MIT
/*
 * LD_PRELOAD experiment: force hipHostMalloc allocations to be coherent.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <hip/hip_runtime_api.h>

static hipError_t (*real_hipHostMalloc)(void **ptr, size_t size,
					unsigned int flags);
static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;

static void resolve_syms(void)
{
	void *hip = dlopen("libamdhip64.so", RTLD_NOW | RTLD_NOLOAD);

	if (!hip)
		hip = dlopen("libamdhip64.so.7", RTLD_NOW | RTLD_NOLOAD);
	if (!hip)
		hip = dlopen("libamdhip64.so", RTLD_NOW | RTLD_LOCAL);
	if (!hip)
		hip = dlopen("libamdhip64.so.7", RTLD_NOW | RTLD_LOCAL);
	real_hipHostMalloc = dlsym(hip ? hip : RTLD_NEXT, "hipHostMalloc");
	fprintf(stderr, "HIP_COHERENT_SHIM hipHostMalloc=%p\n",
		real_hipHostMalloc);
}

hipError_t hipHostMalloc(void **ptr, size_t size, unsigned int flags)
{
	unsigned int new_flags;
	hipError_t ret;

	pthread_once(&resolve_once, resolve_syms);
	if (!real_hipHostMalloc)
		return hipErrorUnknown;

	new_flags = flags;
	if (new_flags & hipHostMallocNonCoherent) {
		new_flags &= ~hipHostMallocNonCoherent;
		new_flags |= hipHostMallocCoherent;
	}
	ret = real_hipHostMalloc(ptr, size, new_flags);
	fprintf(stderr,
		"HIP_COHERENT_SHIM hipHostMalloc size=%zu flags=0x%x -> 0x%x ret=%d ptr=%p\n",
		size, flags, new_flags, ret, ptr ? *ptr : NULL);
	return ret;
}
