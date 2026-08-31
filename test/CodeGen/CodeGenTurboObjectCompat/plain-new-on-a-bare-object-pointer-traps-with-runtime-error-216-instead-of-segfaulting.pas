(*
Issue #514: a PLAIN New(p) (no extended New(P, Init(...)) syntax -- #511's
own vptr-stamping, CGProcCall.cpp, only ever reaches THAT path) on a
pointer to an object type never stamps a `_vptr` at all.  plang_new's own
calloc (runtime/plang_sys.cpp) leaves the slot NULL, not garbage, so
before this fix a later virtual call through it read a function pointer
from memory near address zero and segfaulted (confirmed on unmodified
main: exit 139, core dumped).  Real Borland/FPC accepts the identical
program too -- with "Warning: use extended syntax of NEW and DISPOSE for
instances of objects" -- and cleanly traps "Runtime error 216: General
protection fault" the instant the virtual call actually happens (confirmed
against a local `fpc -Mtp` build: exit 216, no core dump).  Fixed by
reusing the exact same nil-pointer guard and error code every other
bad-pointer access in this codebase already routes through
(RangeCheckGuards::emitNilCheck, the same mechanism
nil-dereference-aborts-with-exit-code-216-not-the-shared-status.pas
covers) at the virtual-dispatch call site's own vptr load -- see
CGFuncCall::emitMethodCallExpr's own comment for the whole design -- not by
touching the plain New(p) path itself, which already begins zero-filled.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %checkexit 216 %run %t.O0 > %t.O0.out 2> %t.O0.err
RUN: FileCheck %s < %t.O0.out
RUN: FileCheck --check-prefix=ERR %s < %t.O0.err
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %checkexit 216 %run %t.O2 > %t.O2.out 2> %t.O2.err
RUN: FileCheck %s < %t.O2.out
RUN: FileCheck --check-prefix=ERR %s < %t.O2.err
*)

program plainnew;
type
  TAnimal = object
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;
  PDog = ^TDog;
var
  P: PDog;

procedure TAnimal.Speak;
begin
  writeln('Animal speaks');
end;

procedure TDog.Speak;
begin
  writeln('Dog speaks');
end;

begin
  New(P);
  writeln('about to dispatch through an unstamped vptr');
  P^.Speak;
  writeln('unreachable');
end.

(*
CHECK:about to dispatch through an unstamped vptr
CHECK-NOT:unreachable
ERR:Runtime error 216
*)
