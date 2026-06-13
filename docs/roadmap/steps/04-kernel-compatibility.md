# Broaden kernel version compatibility

**Roadmap step 4.** Kernel compatibility now uses feature-detected Thunderbolt
shims so release builds can run on stock distro kernels without maintainer-tree
only helpers.

### Motivation
The module needs Linux 6.14+ (or the flake's `linux-thunderbolt`) due to
maintainer-tree Thunderbolt/USB4 changes, limiting adoption on stock distro
kernels.

### Scope
- Isolate the maintainer-tree dependencies behind compatibility shims.
- Feature-detect `tb_ring_throttling()` and friends.
- Document the minimum viable kernel per feature.

### Kernel/feature compatibility matrix

| Feature | Minimum kernel / requirement | Behavior when unavailable |
| --- | --- | --- |
| Module build/load and ring/path setup | Linux 6.1+ stock kernels with Thunderbolt service/ring APIs (tested on 6.17) | Module does not load if core Thunderbolt symbols are missing |
| `nhi_interrupt_throttle_ns` ring interrupt throttling | Linux 6.14+ / kernels exporting `tb_ring_throttling()` | Parameter is accepted but ignored; default NHI interrupt throttling remains active |
| Minimal TBnet identity MAC PHY-port derivation | Linux 6.1+ (shimmed `(link - 1) / TB_LINKS_PER_PHY_PORT`) | No maintainer-tree dependency on `tb_phy_port_from_link()` |

### Acceptance criteria
- [ ] A compatibility matrix in docs.
- [ ] The module builds and loads on at least one older stock kernel with
      degraded-but-documented features.

### Labels
`kernel`, `compatibility`
