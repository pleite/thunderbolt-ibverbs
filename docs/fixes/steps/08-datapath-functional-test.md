# Add an automated data-path functional test to CI (T1, T3)

**Driver fix T1 + T3.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
CI builds/installs the module + provider across distros but runs no RDMA
data-path test (`.github/workflows/release-artefacts.yml`); the smoke tests only
exercise wire parsing. Functional correctness is validated manually on two
physical machines, not in CI.

### Scope
- Add a loopback/virtual two-node harness (QEMU or a software backend) in CI.
- Run `ib_write_bw`/`ib_send_bw` plus a bit-verified transfer.
- Assert zero error counters.

### Acceptance criteria
- [ ] CI runs a real verbs transfer and bit-verifies the result.
- [ ] The job fails on any error-counter movement.

### Labels
`testing`, `ci`
