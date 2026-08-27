(*
Issue #146: issue #123 gave Sema::checkExpr a depth guard (ExprDepthScope,
MaxExprDepth = 1000, SemaExpr.cpp) for a flat `1+1+1+...+1` chain, but that
only bounds Sema -- a diagnostic-free expression under that 1000-term cap
still reaches CGExprCore::emitExpr / CGBinaryOps::emitBinary afterwards,
whose own mutual recursion had no guard at all.  On an ordinary Release/Debug
build that never mattered: CodeGen's per-frame stack cost keeps its own
crash threshold well above 1000 terms. Under this project's own sanitizer CI
build (-DPLANG_SANITIZE=address,undefined), though, ASan's much larger
per-frame cost drops that threshold down to a few hundred terms -- well
under Sema's cap -- so this file's 300-term chain (comfortably under 1000,
so Sema accepts it with no diagnostic; comfortably over CodeGen's own
sanitizer-build MaxExprDepth of 200, see CGExprCore.h) used to crash the
compiler outright with a raw SIGSEGV / ASan stack-overflow report instead of
either compiling or failing cleanly.

A crash surfaces as a real signal (SIGSEGV etc.), not just a nonzero exit
code -- plain `not` (without --crash) already distinguishes the two: it
fails the whole step if the wrapped command crashes via signal, rather than
treating a crash as just another kind of "the inverted command succeeded".
So the step below only passes if the compiler exits via a controlled,
non-crashing diagnostic, matching
test/Driver/SemaRobustness/deeply-chained-addition-does-not-crash-the-compiler.pas's
own precedent for Sema's guard.

See deeply-chained-addition-still-compiles-on-a-normal-build.pas next to
this file for the companion check that a normal (non-sanitizer) build still
compiles the very same input, proving CodeGen's sanitizer-only ceiling does
not regress ordinary builds.
*)

(*
REQUIRES: asan-build
*)

(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: depth ceiling
*)

program p;
var x: integer;
begin
  x := 1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1;
  writeln(x)
end.
