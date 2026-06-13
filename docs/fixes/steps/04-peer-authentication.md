# Add peer authentication and per-peer ACLs (S2, S3)

**Driver fix S2 + S3.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
Peer trust is purely Thunderbolt topology: peers match on backend + `tb_xdomain`
only (`kernel/peer.c:45-49`), and endpoint validation checks only a plaintext
QPN (`kernel/ibdev.c:1142`). A rogue device or spoofed QPN can therefore send
RDMA traffic. This is the epic that adds an actual authentication layer; it
builds on the `peer_allowlist` module param already present.

### Scope
- Add a connection handshake (PSK / DTLS-style) establishing a per-peer session.
- Bind QPN/endpoint acceptance to the authenticated session.
- Provide per-peer ACLs beyond topology; file sub-issues for sub-parts.

### Acceptance criteria
- [ ] An unauthenticated peer cannot establish a usable connection.
- [ ] Endpoint acceptance is tied to the authenticated session.
- [ ] A threat-model section documents the new boundary.

### Labels
`security`, `kernel`, `epic`
