# Security policy

`thunderbolt-ibverbs` is a research driver and is **not** production-hardened.
See [docs/SECURITY.md](docs/SECURITY.md) for implementation details of the
current hardening model.

## Reporting a vulnerability

Please report potential security issues privately through GitHub Security
Advisories for this repository.

If private reporting is unavailable, open an issue marked `security` and avoid
including exploit details until maintainers can coordinate disclosure.

## Threat model (summary)

- Local host kernel and local userspace that own PD/MR resources are trusted.
- A remote host connected over Thunderbolt/USB4 is treated as untrusted.
- The project aims to prevent out-of-bounds memory access and cross-peer key
  abuse across the transport boundary.

Tracked security findings and remediations are recorded in
[`docs/FINDINGS.md`](docs/FINDINGS.md).
