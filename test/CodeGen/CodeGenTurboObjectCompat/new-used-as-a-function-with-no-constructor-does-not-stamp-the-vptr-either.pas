(*
Issue #622/#514: 'P := New(PDog)' -- the function form of New with NO
constructor argument -- is plain-new-on-a-bare-object-pointer-traps-with-
runtime-error-216-instead-of-segfaulting.pas's own sibling: it does NOT
run any constructor (confirmed against a local fpc -Mtp build: a
declared-but-unrun constructor leaves fields at whatever plang_new's own
calloc zero-filled them to) and does NOT stamp a '_vptr' either -- matched
here, not "fixed" to be safer, per this project's own field-practice
policy (issue #514's own comment, CGProcCall.cpp).  Only the SECOND-
argument (constructor) form of function-New reaches CGProcCall::
emitNewObjectValue's own StampVptr/StampFieldVptrs call; this is the
'CtorArg == nullptr' early return, proven the same way #514's own test
proves the statement form: a later virtual call through the result traps
Runtime error 216 rather than dispatching to a real override.
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

program plainnewfunc;
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
  P := New(PDog);
  writeln('about to dispatch through an unstamped vptr');
  P^.Speak;
  writeln('unreachable');
end.

(*
CHECK:about to dispatch through an unstamped vptr
CHECK-NOT:unreachable
ERR:Runtime error 216
*)
