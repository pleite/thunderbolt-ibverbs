# Broaden kernel version compatibility

**Roadmap step 4.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The module needs Linux 6.14+ (or the flake's `linux-thunderbolt`) due to
maintainer-tree Thunderbolt/USB4 changes, limiting adoption on stock distro
kernels.

### Scope
- Isolate the maintainer-tree dependencies behind compatibility shims.
- Feature-detect `tb_ring_throttling()` and friends.
- Document the minimum viable kernel per feature.

### Acceptance criteria
- [ ] A compatibility matrix in docs.
- [ ] The module builds and loads on at least one older stock kernel with
      degraded-but-documented features.

### Labels
`kernel`, `compatibility`
