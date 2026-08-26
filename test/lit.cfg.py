# lit.cfg.py -- static config for plang's lit-based test suite (see
# test/README.md and issue #34 for what lives here vs. in test/unittests/).
#
# Built on lit.llvm, the same helper Clang/MLIR/Flang use in their own
# out-of-tree lit.cfg.py: it wires FileCheck/not/split-file onto PATH and
# into config.substitutions regardless of whether a given install bundles
# them with a system LLVM package (as this project's own dev machine does)
# or ships them separately (pip-installed lit next to a system LLVM that
# only provides the C++ tools -- common on some distros).

import os
import shutil

import lit.formats
import lit.llvm

config.name = "plang"
config.suffixes = [".pas"]

# %plang and FileCheck are real ELF binaries, but lit's internal shell still
# runs them fine -- it only reimplements shell BUILTINS (cd, echo, &&, |,
# redirection), not the programs a RUN line invokes.  execute_external=False
# (what LLVM/Clang/MLIR use themselves) buys two things a real /bin/sh does
# not: identical RUN-line behavior on Linux and macOS (this project's own CI
# matrix) without depending on /bin/sh differences between them, and a hung
# child killed cleanly by lit's own timeout rather than a leaked subshell
# pipeline -- both real considerations, not just precedent-following, given
# this suite already has at least one intentionally-hangable test (a
# stdin-blocked program under a 5s watchdog).
config.test_format = lit.formats.ShTest(execute_external=False)

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.plang_binary_dir, "test")

lit.llvm.initialize(lit_config, config)
llvm_config = lit.llvm.llvm_config  # a global set by initialize(), not a class to construct
llvm_config.use_default_substitutions()
llvm_config.add_tool_substitutions(
    ["FileCheck", "not", "split-file"], [config.llvm_tools_dir])

# ---- %plang and friends -----------------------------------------------
#
# PLANG_TEST_EXTRA_FLAGS / PLANG_TEST_RUN_WRAPPER: the same two env vars the
# original GoogleTest harness's compileAndRunFile() read (DriverHarness.h,
# since deleted along with the GTest files it served), so the CI
# jobs that re-run the whole suite at -O1/-O2/-O3, and the guardheap-wrapped
# subset, keep working across the migration with no change to those jobs'
# env -- only which suite answers to it changes.  --param is lit's own more
# idiomatic equivalent (lit --param optlevel=-O2); support both rather than
# forcing a simultaneous CI-script rewrite, matching CI's own existing name.
plang_extra_flags = lit_config.params.get(
    "optlevel", os.environ.get("PLANG_TEST_EXTRA_FLAGS", ""))
plang_exe = os.path.join(config.plang_tools_dir, "plang")

# Registration order matters: lit substitution is plain substring
# replacement, not tokenized, so a shorter substitution earlier in
# config.substitutions than a longer one that starts with it corrupts the
# longer one (%plang, matched first, turns %plang_ep into "<plang-path>_ep"
# -- caught for real running this very smoke test against the first draft,
# which used config.substitutions.insert(0, ...) newest-last and got this
# exact ordering backwards). Longest/most-specific first, always -- inserted
# as one block so the list reads top-to-bottom in match order, not scattered
# insert(0, ...) calls whose net order isn't obvious from the diff.
# %plang_ir: deliberately excludes plang_extra_flags. The GTest harness's
# compileFileToIR() (what every compileAndEmitIR-based test used) never read
# PLANG_TEST_EXTRA_FLAGS at all -- only compileAndRunFile() did -- so the
# `optimized` CI job's -O1/-O2/-O3 re-run never touched raw-IR-shape checks
# before this migration. %plang's own extra_flags substring would silently
# start doing so if IR-substring tests used it instead: found for real when
# the -O1/-O2/-O3 job broke on 3 CHECK-DAG tests expecting unoptimized
# `phi i1`/`icmp sge` patterns that -O1 had already simplified away. Must be
# registered before %plang below -- same longest/most-specific-first reason
# %plang_ep is.
config.substitutions[0:0] = [
    ("%plang_ir", f"{plang_exe}".strip()),
    ("%plang_ep_run", f"{plang_exe} -std=iso10206 {plang_extra_flags} %s -o %t && %t".strip()),
    ("%plang_run", f"{plang_exe} {plang_extra_flags} %s -o %t && %t".strip()),
    ("%plang_ep", f"{plang_exe} -std=iso10206 {plang_extra_flags}".strip()),
    ("%plang", f"{plang_exe} {plang_extra_flags}".strip()),
]

# %run: the one place PLANG_TEST_RUN_WRAPPER (e.g. the guardheap allocator,
# test/tools/run-under-guardheap.sh) can hook the EXECUTION step alone, the
# way DriverHarness.h's runWrapperPrefix() already does for every GoogleTest
# caller today.  A no-op by default; every RUN line that runs a just-built
# program should spell it "%run %t", never a bare "%t", or this wrapper
# silently stops applying to that file with no failure signal -- see
# issue #34's own risk notes.
config.substitutions.insert(0,
    ("%run", os.environ.get("PLANG_TEST_RUN_WRAPPER", "")))

# %hold_stdin_open: the one case this suite's own %t/%run idiom can't
# express directly -- connecting a compiled program's stdin to a pipe that
# never sends EOF and never has anything queued (a real terminal's own
# behavior before a keystroke, unlike /dev/null's immediate EOF), with a
# watchdog in case the program under test actually does block forever.
# Needs real job control (&, $!, wait) lit's own restricted internal shell
# doesn't implement, so it's a real, external bash script (matching
# test/tools/run-under-guardheap.sh's own precedent) rather than an
# in-line RUN: sequence.
config.substitutions.insert(0,
    ("%hold_stdin_open",
     "bash " + os.path.join(config.plang_source_dir, "test", "tools", "run-with-stdin-held-open.sh")))

# ---- available_features ------------------------------------------------
#
# fpc-binary, not bare "fpc": include/plang/Basic/Dialects.def already
# reserves "fpc" for a future, unrelated -std=fpc plang dialect flag: using
# the same word for "the real fpc binary is on PATH" would be genuinely
# ambiguous once both exist.  Gated on a real `fpc -iV` invocation
# succeeding, not just shutil.which -- a present-but-broken install (no
# fpc.cfg, can't find its RTL units) should degrade the same way a missing
# one does, not fail every test in test/Compat/FPC/ identically for a reason
# unrelated to what each one is actually testing.
if shutil.which("fpc") is not None:
    import subprocess
    try:
        subprocess.run(["fpc", "-iV"], capture_output=True, timeout=5, check=True)
        config.available_features.add("fpc-binary")
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError):
        pass

if os.name != "nt":
    config.available_features.add("posix")
