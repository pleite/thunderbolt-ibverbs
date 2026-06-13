# Packaging and release automation

**Roadmap step 7.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
Debian, Fedora, Arch, and Nix builds already exist; keeping them in lockstep
and releasing cleanly is ongoing maintenance.

### Scope
- Verify DKMS + provider packages across the supported distros each release.
- Keep `packaging/` and `nix/` in sync.
- Tighten the release-artefact workflow.

### Acceptance criteria
- [ ] A release produces working DKMS and provider packages for every supported
      distro from one workflow.
- [ ] Validated by an install smoke.

### Labels
`packaging`, `ci`, `release`
