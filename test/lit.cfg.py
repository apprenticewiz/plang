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
import sys

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
    # %python: this SAME interpreter lit itself is running under (not a bare
    # "python3", which may not be the one lit resolved python3-pty against,
    # e.g. inside a venv) -- for the one test (Crt's own ReadKey/KeyPressed)
    # that drives a real PTY via the stdlib pty module.
    ("%python", sys.executable),
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

# %kill_during_compile: issue #278's own regression coverage -- interrupting
# a real compile with SIGTERM partway through and checking the driver's
# scratch TMPDIR for what got left behind. Needs real job control (&, $!,
# wait) to signal the driver's whole process group, not just the driver
# itself (its re-invoked-as-"-pc1" front end and llc are separate child
# processes that a single-pid signal would otherwise orphan), so this is a
# real, external bash script rather than an in-line RUN: sequence, same as
# %hold_stdin_open just above.
config.substitutions.insert(0,
    ("%kill_during_compile",
     "bash " + os.path.join(config.plang_source_dir, "test", "tools", "kill-during-compile.sh")))

# %checkexit: the one case this suite's own %t/%run idiom can't express
# directly -- pinning a RUN line's exit status to a SPECIFIC number rather
# than just "zero" (a bare RUN: line) or "nonzero" (`not`).  Turbo's
# numbered run-time errors (RangeCheckGuards.cpp's plang_tp_runerror,
# CGProcCall.cpp's RunError builtin) are exactly this: 200 has to stay
# distinct from 201/215/216, not just "some failure". Needs a real,
# external script for the same reason %hold_stdin_open/%kill_during_compile
# (just below) do -- see check-exit-code.sh's own comment for what lit's
# internal shell cannot do here (no `$?`, and `not` has no equivalent of
# `--exit-code=<n>`).
config.substitutions.insert(0,
    ("%checkexit",
     "bash " + os.path.join(config.plang_source_dir, "test", "tools", "check-exit-code.sh")))

# %timed_run_at_least: Crt's own Delay(MS) (Turbo Tier 4, Cluster C item 5)
# is a real wait, checked by timing a run from outside it -- needs a real
# script for the same reasons %checkexit/%hold_stdin_open just above do (no
# real `$( )` arithmetic in lit's own internal shell, and a literal '%' in
# an in-line RUN: line collides with lit's own %s/%t/... substitution
# scanning before any shell/quoting is involved) -- see
# timed-run-at-least-ms.sh's own comment.
config.substitutions.insert(0,
    ("%timed_run_at_least",
     "bash " + os.path.join(config.plang_source_dir, "test", "tools", "timed-run-at-least-ms.sh")))

# %run_under_pty: Crt's own KeyPressed/ReadKey raw-mode behavior (Turbo Tier
# 4, Cluster C item 5), tested against a REAL pseudo-terminal rather than a
# pipe -- see run-under-pty.py's own comment for why a pipe cannot exercise
# this. Gated by test authors on the python3-pty feature above.
config.substitutions.insert(0,
    ("%run_under_pty",
     f"{sys.executable} " + os.path.join(config.plang_source_dir, "test", "tools", "run-under-pty.py")))

# Issue #130's gdb pretty-printer -- referenced straight from the source
# tree (share/plang/gdb/), not the installed copy, same as every other
# %substitution here points at the just-built binary rather than anything
# `make install` may or may not have run yet.
config.substitutions.insert(0,
    ("%plang_schema_printers",
     os.path.join(config.plang_source_dir, "share", "plang", "gdb", "plang_schema_printers.py")))

# Turbo Tier 4, Cluster C item 7: the shipped Printer/Strings units, same
# "straight from the source tree, not the installed copy" reasoning as
# %plang_schema_printers just above -- a lit run against the in-tree
# build-dir binary has no installed <prefix>/lib/plang/units tier to find
# these through on its own (the identical reason
# a-uses-clause-resolves-against-plang-unit-dir-with-no-i-flag.pas's own
# comment gives for using PLANG_UNIT_DIR rather than relying on the default
# search path), so a test that wants the REAL shipped Printer.pas/Strings.pas
# -- not a throwaway stand-in -- points PLANG_UNIT_DIR at this directly.
config.substitutions.insert(0,
    ("%plang_units_dir",
     os.path.join(config.plang_source_dir, "share", "plang", "units")))

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

if shutil.which("gdb") is not None:
    import subprocess
    try:
        subprocess.run(["gdb", "--version"], capture_output=True, timeout=5, check=True)
        config.available_features.add("gdb-binary")
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError):
        pass

if os.name != "nt":
    config.available_features.add("posix")

# python3-pty: whether this python3 has the stdlib `pty` module (POSIX
# pseudo-terminal support). Turbo Tier 4, Cluster C item 5's Crt unit uses
# real termios raw-mode terminal state for ReadKey/KeyPressed
# (runtime/plang_crt.cpp's own ensureRawMode); piped stdin is never a tty,
# so a test that only pipes input could not tell a real raw-mode bug apart
# from tcgetattr/tcsetattr silently no-op'ing on a non-tty. A PTY is the
# only portable way to test that for real without a genuine interactive
# terminal, and this project has no existing PTY-testing infrastructure of
# its own to build on -- probed the same direct way fpc-binary/gdb-binary
# above are, rather than assumed present.
try:
    import subprocess
    subprocess.run([sys.executable, "-c", "import pty"],
                    capture_output=True, timeout=5, check=True)
    config.available_features.add("python3-pty")
except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError):
    pass

# dev-full: /dev/full is a Linux (and some other Unix) character device that
# accepts any open but fails every write with ENOSPC -- the only portable way
# to make a write fail *after* a successful open, which is exactly the gap
# issue #246 found (a failed OPEN was already diagnosed; a failed WRITE was
# not). Not present on macOS, so this is probed directly rather than assumed
# from the platform name, the same way fpc-binary/gdb-binary above are.
try:
    import stat
    if stat.S_ISCHR(os.stat("/dev/full").st_mode):
        config.available_features.add("dev-full")
except OSError:
    pass

# asan-build: issue #146's CodeGen expression-depth guard (CGExprCore.h)
# uses a much lower MaxExprDepth under a sanitizer build, where its real
# per-frame stack cost is far higher than on a normal build, than on one
# without -- so a test exercising that guard needs to know which case it is
# running under to pick the right expectation (a normal build has plenty of
# headroom and just compiles the same input; a sanitizer build's tighter
# ceiling turns what used to be a raw SIGSEGV into a clean diagnostic
# instead). Read straight out of this build tree's own CMakeCache.txt rather
# than re-deriving it (e.g. from compiler flags), the same directness
# fpc-binary/gdb-binary above use for what they gate on.
try:
    with open(os.path.join(config.plang_binary_dir, "CMakeCache.txt")) as f:
        _cmake_cache = f.read()
    for _line in _cmake_cache.splitlines():
        if _line.startswith("PLANG_SANITIZE:") and _line.split("=", 1)[1] not in ("", "OFF"):
            config.available_features.add("asan-build")
            break
except OSError:
    pass
