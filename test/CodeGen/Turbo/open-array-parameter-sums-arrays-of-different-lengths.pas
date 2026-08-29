(*
Turbo's own open-array parameter (procedure P(a: array of Integer)), NOT
EP/ISO 7185 Level 1's conformant-array-schema form -- reuses
TypeKind::ConformantArray (Type::IsOpenArray) so CodeGen's existing
ptr+lo+hi calling convention and Sema's existing bound-identifier machinery
carry over almost unchanged, but the lower bound is always 0 (never a named
bound variable the caller supplies) and only the actual's high bound
travels -- confirmed empirically against fpc -Mtp.  Called here with two
arrays of DIFFERENT lengths and different declared lower bounds (1..3 and
0..4), each computing over its own actual length via Low(a)/High(a) rather
than any fixed size -- and, outside the call, High/Low of the ORIGINAL
fixed array x3 still answer with ITS OWN declared bounds (3, 1), unaffected
by what an open-array parameter sees them as internally.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --match-full-lines %s
*)

program p;
function Sum(a: array of Integer): Integer;
var i, s: Integer;
begin
  s := 0;
  for i := Low(a) to High(a) do s := s + a[i];
  Sum := s;
end;

var
  x3: array[1 .. 3] of Integer;
  x5: array[0 .. 4] of Integer;
begin
  x3[1] := 1; x3[2] := 2; x3[3] := 3;
  x5[0] := 10; x5[1] := 20; x5[2] := 30; x5[3] := 40; x5[4] := 50;
  writeln(Sum(x3));
  writeln(Sum(x5));
  writeln(High(x3), ' ', Low(x3));
end.

(*
CHECK:6
CHECK-NEXT:150
CHECK-NEXT:3 1
*)
