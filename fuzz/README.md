# Fuzz targets (issue #183 Phase 1)

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
