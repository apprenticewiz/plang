# Fuzz targets (issue #183)

Three libFuzzer targets, built only when configured with
`-DPLANG_ENABLE_FUZZERS=ON` (Clang only):

- `scanner-fuzzer.cpp` — raw bytes through `Scanner`'s in-memory-buffer constructor.
- `parser-fuzzer.cpp` — raw bytes through `Scanner` then `Parser::parse()`.
- `pmi-fuzzer.cpp` — raw bytes through `Sema::loadPMIFromBuffer` (EP `.pmi`
  interface loading); see that file's own header comment for why a
  buffer-taking core was split out of `Sema::loadPMI` for this.

Each has a seed corpus under `corpus/<target>/`.

See `docs/technical_info.md`'s "Fuzzing" section for how to configure, build,
and run these for longer than CI's short per-PR smoke budget, and for the
`ASAN_OPTIONS=alloc_dealloc_mismatch=0` workaround a fuzzing run needs.

## CI (issue #183 Phases 1 and 2)

- `.github/workflows/ci.yml`'s `fuzz-smoke` job (Phase 1): every PR, 60s per
  target against the checked-in `corpus/<target>/` seeds. A crash-regression
  smoke check, not deep fuzzing.
- `.github/workflows/fuzz-scheduled.yml` (Phase 2): nightly (plus manual
  `workflow_dispatch`), 5 minutes per target by default, against a corpus
  that persists and grows across runs via `actions/cache` (seeded from
  `corpus/<target>/` only on the first run / a cache miss) rather than
  resetting to the checked-in seeds every time. If a run finds a crash, the
  job fails, the crashing input is uploaded as a build artifact, and the job
  log prints the same local-repro recipe as the "Reproducing a crash found
  by CI" section below. See that workflow file's own comments for the exact
  budget numbers and reasoning.

## Reproducing a crash found by CI

1. Download the failed run's `fuzz-crashes-<run id>` artifact (GitHub
   Actions run summary page → Artifacts).
2. Build the fuzz targets locally (see `docs/technical_info.md`'s "Fuzzing"
   section, linked above, for the exact commands).
3. Replay the downloaded file directly against the matching target:
   ```bash
   ASAN_OPTIONS=alloc_dealloc_mismatch=0 \
     ./build-fuzz/bin/<target>-fuzzer fuzz-crashes/<target>-crash-<hash>
   ```
   This is a single deterministic run of the target on that one input (no
   fuzzing loop), so it's safe to run under gdb/lldb, rr, etc.
4. Once root-caused and fixed, consider minimizing the crashing input
   (`<target>-fuzzer -minimize_crash=1 ...`, see libFuzzer's own docs) and
   adding it to `corpus/<target>/` as a permanent regression seed, so both
   CI jobs above catch any future regression.
