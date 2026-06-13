# thunderbolt-ibverbs roadmap

The big-picture plan for the project, kept here so we can reference it when
deciding what to work on next and how the pieces fit together.

`thunderbolt-ibverbs` is a research-grade Linux kernel module plus a userspace
libibverbs provider that emulates an InfiniBand RDMA verb device across generic
USB4 / Thunderbolt DMA rings. The driver works today (see the
[Hellas blog post](https://blog.hellas.ai/blog/thunderbolt-ibverbs/)) but is, by
its own README, "buggy, insecure, and not for production". This roadmap turns
that warning into a concrete list of work.

## How to use this file

Each numbered step below is written so it can be filed directly as a GitHub
**issue** (or a tracking **feature/epic** with sub-issues) and worked on through
its own **pull request**. For every step there is:

- a short motivation (why it matters),
- the scope of work (what to change), and
- acceptance criteria (how we know it is done),
- suggested labels.

When a step is picked up, open an issue using the step title, link the PR back
to this file, and check the box here once the PR merges.

> Note: step 0 ("establish the roadmap") is this document itself, included so the
> plan is self-referential and the meta-work is tracked like any other step.

## Status legend

- [ ] not started
- [~] in progress
- [x] done

## Steps

### 0. Establish the roadmap and docs reference
- [~] **Motivation:** there is no single place that captures the big picture, so
  planning happens ad-hoc. A referenced plan file lets contributors and agents
  align on direction.
- **Scope:** add this `docs/ROADMAP.md`, link it from `README.md`, and define the
  issue/feature/PR-per-step workflow.
- **Acceptance criteria:** `docs/ROADMAP.md` exists, is linked from the README,
  and lists each subsequent step as an issue-ready spec.
- **Labels:** `documentation`, `meta`.

### 1. Security threat model and hardening
- [ ] **Motivation:** the README flags the driver as insecure. RDMA over a DMA
  fabric exposes memory to a peer; without a threat model this stays unfit for
  any shared or untrusted setting.
- **Scope:** document the trust boundary between connected hosts, audit memory
  registration / remote key handling, bound what a remote peer can read/write,
  and add guardrails (key scoping, length checks, optional peer allow-listing).
- **Acceptance criteria:** a `docs/SECURITY.md` (or section) describing the
  threat model; identified issues filed as sub-issues; at least the highest-risk
  memory-access paths bounded and tested.
- **Labels:** `security`, `kernel`, `epic`.

### 2. Native Linux-to-Linux transport hardening
- [ ] **Motivation:** the native Linux transport is the main path and most of the
  benchmark story depends on it; bugs here block everyone.
- **Scope:** harden ring setup/teardown, error and disconnect handling, and the
  verb completion paths; add stress and fault-injection coverage; reduce known
  buggy edge cases under cable pulls and module reload.
- **Acceptance criteria:** repeated connect/disconnect and module reload cycles
  pass without leaks or oopses; perftest verbs (read/write/send) run clean in CI
  or a documented manual matrix.
- **Labels:** `kernel`, `reliability`, `epic`.

### 3. Apple-compatible transport stabilization
- [ ] **Motivation:** the Apple-compatible transport exists but is experimental;
  stabilizing it widens the usable hardware matrix.
- **Scope:** finish the `mac_compat` profile path, document supported macOS/Apple
  hardware, and add an interop smoke test against a Linux peer.
- **Acceptance criteria:** documented working Apple↔Linux verb exchange; the
  experimental caveat in the README narrowed to known limitations.
- **Labels:** `kernel`, `apple`, `epic`.

### 4. Broaden kernel version compatibility
- [ ] **Motivation:** the module needs Linux 6.14+ (or the flake's
  `linux-thunderbolt`) due to maintainer-tree Thunderbolt/USB4 changes, limiting
  adoption on stock distro kernels.
- **Scope:** isolate the maintainer-tree dependencies behind compatibility shims,
  feature-detect `tb_ring_throttling()` and friends, and document the minimum
  viable kernel per feature.
- **Acceptance criteria:** a compatibility matrix in docs; the module builds and
  loads on at least one older stock kernel with degraded-but-documented features.
- **Labels:** `kernel`, `compatibility`.

### 5. Performance tuning and tunable defaults
- [ ] **Motivation:** parameters like `nhi_interrupt_throttle_ns`, write/fragment
  striping, and `zcopy_min_bytes` materially affect bandwidth/latency but lack
  guidance and good defaults.
- **Scope:** sweep the key tunables, capture results in `bench/`, and pick
  sensible defaults; document the tuning knobs and when to change them.
- **Acceptance criteria:** reproducible benchmark sweep checked into `bench/`,
  updated default values, and a tuning section in the docs.
- **Labels:** `performance`, `benchmarks`.

### 6. Automated end-to-end and regression testing
- [ ] **Motivation:** much validation is manual two-node testing; regressions are
  easy to miss between releases.
- **Scope:** extend the packaged smoke helpers (`tbv_vllm_smoke.sh`,
  `tbv_perftest_runner`) into a repeatable two-node regression suite, and wire as
  much as possible into CI (or a documented self-hosted runner).
- **Acceptance criteria:** a single command runs the verb + transport smoke and
  fails on regression; results are recorded per run.
- **Labels:** `testing`, `ci`.

### 7. Packaging and release automation
- [ ] **Motivation:** Debian, Fedora, Arch, and Nix builds already exist; keeping
  them in lockstep and releasing cleanly is ongoing maintenance.
- **Scope:** verify DKMS + provider packages across the supported distros each
  release, keep `packaging/` and `nix/` in sync, and tighten the release-artefact
  workflow.
- **Acceptance criteria:** a release produces working DKMS and provider packages
  for every supported distro from one workflow, validated by an install smoke.
- **Labels:** `packaging`, `ci`, `release`.

### 8. User and contributor documentation
- [ ] **Motivation:** onboarding depends on the README and the blog post; deeper
  architecture and troubleshooting docs are missing.
- **Scope:** add an architecture overview (kernel module ↔ provider ↔ verbs), a
  troubleshooting guide, and a contributing guide that points back to this
  roadmap.
- **Acceptance criteria:** `docs/` contains architecture, troubleshooting, and
  contributing pages, each linked from the README.
- **Labels:** `documentation`.

## Issue / feature / PR workflow

1. Pick a step and open a GitHub issue titled like the step heading; for
   multi-part steps marked `epic`, open a tracking issue plus sub-issues.
2. Add the suggested labels and link the issue back to this file.
3. Do the work on a branch and open a PR that references the issue; update the
   checkbox and status here in the same PR.
4. Merge closes the issue and ticks the box.

## Follow-on work: analysis findings

All eight steps above shipped (PRs #3, #5, #7, #9, #11, #13, #15, #17), so their
automation now lives under
[`scripts/archive/roadmap/`](../scripts/archive/roadmap/). The next wave of work
comes from the driver performance/security analysis in
[`docs/FINDINGS.md`](FINDINGS.md), which catalogues the concrete security (`S*`),
robustness (`R*`), and testing (`T*`) findings with code citations and a
prioritized fix list. Each open finding is filed as its own issue + branch +
draft PR by the [`scripts/fixes/`](../scripts/fixes/) toolkit, the same way these
roadmap steps were.
