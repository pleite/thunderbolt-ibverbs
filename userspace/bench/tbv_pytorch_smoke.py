#!/usr/bin/env python3
"""Small PyTorch distributed smoke for RCCL-on-ROCm.

Run with torch.distributed.run. On ROCm, PyTorch exposes AMD GPUs through the
CUDA API and the "nccl" backend maps to RCCL.
"""

from __future__ import annotations

import os
import time
from typing import Callable

import torch
import torch.distributed as dist


def env_int(name: str, default: int) -> int:
    value = os.environ.get(name)
    if value is None:
        return default
    return int(value, 0)


def env_bool(name: str, default: bool) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() not in {"0", "false", "no", "off"}


def env_sizes() -> list[int]:
    raw = os.environ.get("TBV_TORCH_SIZES", "65536,262144")
    return [int(item, 0) for item in raw.split(",") if item.strip()]


def env_collectives() -> set[str]:
    raw = os.environ.get("TBV_TORCH_COLLECTIVES", "all_to_all")
    aliases = {
        "alltoall": "all_to_all",
        "all_to_all_single": "all_to_all",
    }
    selected = set()
    for item in raw.split(","):
        name = item.strip()
        if name:
            selected.add(aliases.get(name, name))
    return selected


def barrier() -> None:
    dist.barrier()
    torch.cuda.synchronize()


def timed(
    label: str,
    iters: int,
    fn: Callable[[], None],
    bytes_per_rank: int | None = None,
    gpu_timing: bool = True,
) -> None:
    barrier()
    start_event = None
    stop_event = None
    if gpu_timing:
        start_event = torch.cuda.Event(enable_timing=True)
        stop_event = torch.cuda.Event(enable_timing=True)
        start_event.record()

    start = time.perf_counter()
    for _ in range(iters):
        fn()
    if stop_event is not None:
        stop_event.record()
    torch.cuda.synchronize()
    elapsed = time.perf_counter() - start

    gpu_elapsed = None
    if start_event is not None and stop_event is not None:
        gpu_elapsed = start_event.elapsed_time(stop_event) / 1000.0
    barrier()

    if dist.get_rank() == 0:
        per_iter = elapsed / iters
        suffix = ""
        if bytes_per_rank is not None:
            gbps = bytes_per_rank * 8 / per_iter / 1e9
            suffix = f" ({gbps:.2f} Gb/s logical/rank)"
        gpu_suffix = ""
        if gpu_elapsed is not None:
            gpu_per_iter = gpu_elapsed / iters
            gpu_suffix = f" gpu={gpu_per_iter * 1e6:.1f} us/iter"
        print(f"{label}: {per_iter * 1e6:.1f} us/iter{suffix}{gpu_suffix}")


def validate_once(fn: Callable[[bool], None]) -> None:
    barrier()
    fn(True)
    barrier()


def assert_close(tensor: torch.Tensor, expected: float, label: str, validate: bool) -> None:
    if not validate:
        return
    torch.cuda.synchronize()
    actual = tensor.flatten()[0].item()
    if actual != expected:
        raise RuntimeError(f"{label}: expected {expected}, got {actual}")


def run_all_reduce(
    size_bytes: int,
    iters: int,
    rank: int,
    world: int,
    device: torch.device,
    validate: bool,
    prealloc: bool,
    gpu_timing: bool,
) -> None:
    elems = max(1, size_bytes // 4)
    expected = float(world * (world + 1) // 2)
    tensor = torch.empty((elems,), device=device) if prealloc else None

    def one(do_validate: bool = False) -> None:
        local = tensor
        if local is None:
            local = torch.empty((elems,), device=device)
        local.fill_(float(rank + 1))
        dist.all_reduce(local)
        assert_close(local, expected, "all_reduce", do_validate)

    if validate:
        validate_once(one)
    timed(f"all_reduce bytes={size_bytes}", iters, one, gpu_timing=gpu_timing)


def run_all_gather(
    size_bytes: int,
    iters: int,
    rank: int,
    world: int,
    device: torch.device,
    validate: bool,
    prealloc: bool,
    gpu_timing: bool,
) -> None:
    elems = max(1, size_bytes // 4)
    tensor = torch.full((elems,), float(rank + 1), device=device) if prealloc else None
    out = torch.empty((world * elems,), device=device) if prealloc else None

    def one(do_validate: bool = False) -> None:
        local = tensor
        if local is None:
            local = torch.full((elems,), float(rank + 1), device=device)
        if hasattr(dist, "all_gather_into_tensor"):
            local_out = out
            if local_out is None:
                local_out = torch.empty((world * elems,), device=device)
            dist.all_gather_into_tensor(local_out, local)
        else:
            chunks = [torch.empty_like(local) for _ in range(world)]
            dist.all_gather(chunks, local)
            local_out = torch.cat(chunks)
        if do_validate:
            for peer in range(world):
                assert_close(
                    local_out[peer * elems : (peer + 1) * elems],
                    float(peer + 1),
                    "all_gather",
                    do_validate,
                )

    if validate:
        validate_once(one)
    timed(
        f"all_gather bytes={size_bytes}",
        iters,
        one,
        bytes_per_rank=size_bytes * (world - 1),
        gpu_timing=gpu_timing,
    )


def run_all_to_all(
    size_bytes: int,
    iters: int,
    rank: int,
    world: int,
    device: torch.device,
    validate: bool,
    prealloc: bool,
    gpu_timing: bool,
) -> None:
    elems = max(1, size_bytes // 4)
    inp = None
    out = None
    if prealloc:
        in_chunks = [
            torch.full((elems,), float(rank * 1000 + peer), device=device)
            for peer in range(world)
        ]
        inp = torch.cat(in_chunks)
        out = torch.empty_like(inp)

    def one(do_validate: bool = False) -> None:
        local_inp = inp
        local_out = out
        if local_inp is None or local_out is None:
            in_chunks = [
                torch.full((elems,), float(rank * 1000 + peer), device=device)
                for peer in range(world)
            ]
            local_inp = torch.cat(in_chunks)
            local_out = torch.empty_like(local_inp)
        dist.all_to_all_single(local_out, local_inp)
        if do_validate:
            for peer in range(world):
                assert_close(
                    local_out[peer * elems : (peer + 1) * elems],
                    float(peer * 1000 + rank),
                    "all_to_all_single",
                    do_validate,
                )

    if validate:
        validate_once(one)
    timed(
        f"all_to_all_single bytes={size_bytes}",
        iters,
        one,
        bytes_per_rank=size_bytes * (world - 1),
        gpu_timing=gpu_timing,
    )


def main() -> None:
    if not torch.cuda.is_available():
        raise RuntimeError("torch.cuda is not available; ROCm PyTorch not loaded")

    dist.init_process_group(backend=os.environ.get("TBV_TORCH_BACKEND", "nccl"))
    rank = dist.get_rank()
    world = dist.get_world_size()
    local_rank = env_int("LOCAL_RANK", 0)
    iters = env_int("TBV_TORCH_ITERS", 2)
    collectives = env_collectives()
    validate = env_bool("TBV_TORCH_VALIDATE", True)
    prealloc = env_bool("TBV_TORCH_PREALLOC", True)
    gpu_timing = env_bool("TBV_TORCH_GPU_TIMING", True)

    torch.cuda.set_device(local_rank)
    device = torch.device("cuda", local_rank)

    if rank == 0:
        print(
            f"world={world} iters={iters} sizes={env_sizes()} "
            f"collectives={sorted(collectives)} validate={validate} "
            f"prealloc={prealloc} gpu_timing={gpu_timing}"
        )

    for size in env_sizes():
        if "all_reduce" in collectives:
            run_all_reduce(size, iters, rank, world, device, validate, prealloc, gpu_timing)
        if "all_gather" in collectives:
            run_all_gather(size, iters, rank, world, device, validate, prealloc, gpu_timing)
        if "all_to_all" in collectives:
            run_all_to_all(size, iters, rank, world, device, validate, prealloc, gpu_timing)

    dist.destroy_process_group()


if __name__ == "__main__":
    main()
