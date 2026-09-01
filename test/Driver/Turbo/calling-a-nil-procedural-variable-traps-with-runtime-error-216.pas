(*
Issue #646: ClosureAndCallABI::emitProcVarCall loads the callee out of a
procedural variable's own storage and calls through it with no nil check
at all, unlike every other pointer dereference in this codebase
(RangeCheckGuards::emitNilCheck -- see e.g.
nil-dereference-aborts-with-exit-code-216-not-the-shared-status.pas for
an ordinary pointer, and the CodeGenTurboObjectCompat RTE-216 tests for a
nil vptr).  Confirmed on unmodified main: SIGSEGV at -O0, SIGTRAP/ud2 at
-O2.  Real Turbo/FPC (`fpc -Mtp`) reports "Runtime error 216: General
protection fault" -- fixed by nil-checking the loaded entry point with
the SAME RangeCheckGuards::emitNilCheck idiom those other call sites
already use, rather than inventing new error handling here.

RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %checkexit 216 %run %t.O0 > %t.O0.out 2> %t.O0.err
RUN: FileCheck %s < %t.O0.out
RUN: FileCheck --check-prefix=ERR %s < %t.O0.err
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %checkexit 216 %run %t.O2 > %t.O2.out 2> %t.O2.err
RUN: FileCheck %s < %t.O2.out
RUN: FileCheck --check-prefix=ERR %s < %t.O2.err
*)

program p;
type
  TProc = procedure;
var
  q: TProc;
begin
  q := nil;
  writeln('about to call through a nil procedural variable');
  q;
  writeln('unreachable');
end.

(*
CHECK:about to call through a nil procedural variable
CHECK-NOT:unreachable
ERR:Runtime error 216
*)
