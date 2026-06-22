# Contributing

Thank you for your interest in contributing to `thunderbolt-ibverbs`.

## Roadmap

Development is tracked as a series of numbered steps in
[`docs/roadmap/steps/`](roadmap/steps/).  If you want to work on a planned
feature, look for a step file that matches what you have in mind and open a PR
that references it.  If your idea is not on the roadmap, open an issue first so
we can discuss scope before you invest time writing code.

## Prerequisites

You will need:

- A Linux machine with a Thunderbolt 4 or USB4 controller
- Linux 6.14+ kernel headers (or the `linux-thunderbolt` kernel from this flake)
- Build tools: `make`, `gcc`, `dkms`, `git`
- For userspace: `cmake`, `rdma-core` development headers
- Optional but recommended: a second Thunderbolt-capable machine for end-to-end
  testing

See the [README](../README.md) for full install instructions.

## Building

```sh
# Kernel module
make -C kernel KDIR=/lib/modules/$(uname -r)/build

# Userspace provider (cmake)
cmake -B build userspace/usb4_rdma
cmake --build build

# Clean
make -C kernel clean
```

With Nix:

```sh
nix build .#thunderbolt-ibverbs          # kernel module
nix build .#rdma-core-usb4              # libibverbs + provider
nix develop                              # dev shell
```

## Testing

Freestanding `proto/` smoke binaries build and run without kernel headers:

```sh
make -C proto test        # reliability dedup unit test
make -C tools/ci test     # proto/reliability/identity/config smoke binaries
```

After loading the module (`modprobe thunderbolt_ibverbs …`), verify with:

```sh
ibv_devices
ibv_devinfo -d usb4_rdma0
```

For bandwidth and latency numbers:

```sh
# Node A
ib_write_bw -d usb4_rdma0

# Node B
ib_write_bw -d usb4_rdma0 <node-a-address>
```

The bench helper `tbv_vllm_smoke.sh` runs a two-node vLLM smoke test and checks
that RDMA counters increment — see the README for the full invocation.

Check `docs/TROUBLESHOOTING.md` if anything does not work.

## Kernel module style

- Follow the coding style of the surrounding kernel code (Linux kernel style).
- Use `pr_fmt` / `pr_err` / `pr_info` for module log output; do not use
  `printk` directly.
- Every new module parameter needs a `MODULE_PARM_DESC`.
- Security-sensitive paths (peer acceptance, UUID validation) should fail
  closed: reject on error rather than allow by default.

## Commit messages

Use the conventional-commits format:

```
type(scope): short summary

Longer body if needed.
```

Common types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `ci`.

Common scopes: `kernel`, `userspace`, `bench`, `nix`, `packaging`, `docs`.

## Pull requests

- Target the `main` branch.
- Keep PRs focused: one logical change per PR.
- Reference the relevant roadmap step or issue in the PR description.
- The CI runs Debian, Fedora, Arch, and Nix builds — check all pass before
  requesting review.

## Reporting issues

Please include the output of:

```sh
uname -a
lsmod | grep thunderbolt
dmesg | grep -i thunderbolt
ibv_devices
cat /sys/kernel/debug/thunderbolt_ibverbs/summary 2>/dev/null
```

See `docs/TROUBLESHOOTING.md` for the full checklist.
