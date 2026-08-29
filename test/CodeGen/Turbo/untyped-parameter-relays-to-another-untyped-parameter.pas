(*
Relaying an untyped parameter, with no typecast, straight through to
another untyped formal (ZeroIt's own x) is legal Turbo Pascal -- confirmed
against a local fpc -Mtp build, including that this is the ONE bare use of
one Sema::checkIdent's own diagnostic must NOT fire for (checkCallArgs's
own comment).  End-to-end behavioral confirmation that the whole chain --
Relay's own x, forwarded untouched, reaching ZeroIt's variable typecast,
reaching the caller's real array -- actually zeroes the caller's memory
rather than merely compiling.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --match-full-lines %s
*)

program p;
type TByteArray = array[0 .. 3] of Byte;

procedure ZeroIt(var x);
begin
  FillChar(TByteArray(x), 4, 0);
end;

procedure Relay(var x);
begin
  ZeroIt(x);
end;

var
  v: array[0 .. 3] of Byte;
  i: Integer;
begin
  v[0] := 1; v[1] := 2; v[2] := 3; v[3] := 4;
  Relay(v);
  for i := 0 to 3 do write(v[i], ' ');
  writeln;
end.

(*
CHECK:0 0 0 0
*)
