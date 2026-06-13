#!/usr/bin/env bash
# 04-peer-authentication.sh
# Fix for findings S2 + S3 — no peer authentication / plaintext endpoint (P1).
#
# Usage:
#   ./04-peer-authentication.sh
#   DRY_RUN=1 ./04-peer-authentication.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="04"
STEP_SLUG="peer-authentication"
STEP_TITLE="Add peer authentication and per-peer ACLs (S2, S3)"
STEP_LABELS="security,kernel,epic"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
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
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix S2 + S3 — peer authentication and per-peer ACLs

This PR addresses findings **S2** and **S3** in `docs/FINDINGS.md`.

### What this solves
Replaces "a Thunderbolt cable is connected" as the only trust signal with an
authenticated per-peer session that gates endpoint acceptance.

### Planned changes
- Connection handshake (PSK/DTLS-style) + per-peer session state.
- Bind QPN/endpoint validation to the session; add per-peer ACLs.
- Document the trust boundary.

### Acceptance criteria
- [ ] Unauthenticated peers rejected.
- [ ] Endpoint acceptance bound to session.
EOF

STEP_HANDOFF="Implement fixes S2/S3 from docs/FINDINGS.md: add a per-peer authentication handshake (PSK/DTLS-style) and ACLs, bind QPN/endpoint acceptance (kernel/ibdev.c tbv_qp_validate_native_endpoint, kernel/peer.c tbv_peer_matches) to the authenticated session, and document the trust boundary. File sub-issues for sub-parts."

run_fix_step
