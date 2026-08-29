(*
A procedural VARIABLE itself may be captured by a nested procedure through
the ordinary static-link mechanism (ISO Sec6.7.1) like any other outer
variable -- what may NOT happen is storing a NESTED routine's own reference
INTO one (see a-nested-routine-cannot-be-assigned-to-a-procedural-
variable.pas).  Outer assigns its own local procedural variable f a
top-level (non-nested, so legal) routine, and Inner -- reaching f only
through the static link CodeGenProcs.cpp's closure-capture loop builds --
calls through it, confirming the captured VarEntry still carries its
procedural signature (isProcVar/procType, CGSymbolTable::defVar) across
that re-binding and not just at Outer's own declaration.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi:123
*)

program p;

type
  TProc = procedure(x: integer);

procedure Hi(x: integer);
begin
  writeln('hi:', x);
end;

procedure Outer;
var
  f: TProc;
  procedure Inner;
  begin
    f(123);
  end;
begin
  f := Hi;
  Inner;
end;

begin
  Outer;
end.
