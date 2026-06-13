# Security threat model and hardening

**Roadmap step 1.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The README flags the driver as insecure. RDMA over a DMA fabric exposes host
memory to a peer; without a threat model this stays unfit for any shared or
untrusted setting.

### Scope
- Document the trust boundary between connected hosts.
- Audit memory registration / remote key handling.
- Bound what a remote peer can read/write (key scoping, length checks).
- Add optional peer allow-listing.

### Acceptance criteria
- [ ] `docs/SECURITY.md` (or a section) describing the threat model.
- [ ] Identified concrete issues filed as sub-issues of this epic.
- [ ] At least the highest-risk memory-access paths bounded and covered by tests.

### Labels
`security`, `kernel`, `epic`
