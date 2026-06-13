# Native Linux-to-Linux transport hardening

**Roadmap step 2.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The native Linux transport is the main path and most of the benchmark story
depends on it; bugs here block everyone.

### Scope
- Harden ring setup/teardown, error and disconnect handling, and the verb
  completion paths.
- Add stress and fault-injection coverage.
- Reduce known buggy edge cases under cable pulls and module reload.

### Acceptance criteria
- [ ] Repeated connect/disconnect and module reload cycles pass without leaks
      or oopses.
- [ ] perftest verbs (read/write/send) run clean in CI or a documented manual
      matrix.

### Manual stress / fault-injection coverage

Use `tbv_perftest_runner.py --repeat` with the between-repeat hooks to inject
faults during a verb matrix:

```sh
# reload module between repeats on both hosts
tbv_perftest_runner.py ... \
  --repeat 10 \
  --between-repeat-server-cmd 'modprobe -r thunderbolt_ibverbs && modprobe thunderbolt_ibverbs' \
  --between-repeat-client-cmd 'modprobe -r thunderbolt_ibverbs && modprobe thunderbolt_ibverbs'

# force a disconnect window between repeats (cable pull equivalent)
tbv_perftest_runner.py ... \
  --repeat 10 \
  --between-repeat-server-cmd 'echo 0 > /sys/bus/thunderbolt/devices/domain0/authorized; sleep 2; echo 1 > /sys/bus/thunderbolt/devices/domain0/authorized'
```

Documented matrix for step-2 signoff:

- `ib_send_bw`, `ib_write_bw`, `ib_read_bw`
- `--repeat >= 10`
- baseline (no fault hook)
- module reload hook on both hosts
- disconnect hook (one side)

### Labels
`kernel`, `reliability`, `epic`
