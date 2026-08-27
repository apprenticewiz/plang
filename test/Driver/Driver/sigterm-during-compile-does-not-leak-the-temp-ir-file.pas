(*
Issue #278: the driver creates an intermediate .ll (and, past the front
end, a .o) in TMPDIR via llvm::sys::fs::createTemporaryFile whenever
-save-temps was not given, but registered no cleanup for a fatal signal --
only the explicit llvm::sys::fs::remove() calls already on each *normal*
return path, none of which run when the process is killed instead of
returning.  A driver interrupted mid-compile (e.g. Ctrl-C, or a plain
`kill -TERM`) left its temp file behind in TMPDIR indefinitely -- the
review's own repro noted 52 such corpses already sitting on the review
machine from ordinary interrupted compiles.

Fixed by registering every such temp file with llvm::sys::RemoveFileOnSignal
right after creating it, and un-registering it (llvm::sys::DontRemoveFileOnSignal)
once it is removed on a normal path, so a later signal in the same run does
not try to remove an already-gone path.

kill-during-compile.sh sends SIGTERM to the whole compile's process group --
the driver plus the "-pc1" front end it re-invokes itself as, and llc,
which are separate child processes a single-pid signal would otherwise
leave running and orphaned -- as soon as anything appears in a scratch
TMPDIR, then reports whether that directory is empty once everything has
exited.
*)

(*
RUN: split-file %s %t.dir
RUN: %kill_during_compile %t.dir/tmp %plang %t.dir/hello.pas -o %t.dir/prog | FileCheck %s
*)

(*
CHECK: CAUGHT-MID-FLIGHT
CHECK-NEXT: SCRATCH-DIR-CLEAN
*)

//--- hello.pas
program hello;
begin writeln('hi') end.
