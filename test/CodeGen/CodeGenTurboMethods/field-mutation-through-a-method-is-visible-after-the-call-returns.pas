(*
Turbo Tier 5, Cluster A item 4: the single most important behavioral proof
for this item -- a method body writes its own field through the bare,
unqualified name (no 'Self.' needed), CodeGen binds that bare name to a
GEP into the SAME storage the caller's own variable occupies (the receiver's
ADDRESS is what travels as the implicit 'Self' argument, not a copy), so
the mutation is still visible once the call returns and a SECOND method
reads the field back.  A bug in Self's plumbing -- e.g. binding a field to
a copy instead of the caller's own storage -- would compile and run clean
but print the field's ORIGINAL value here, not a compile-time error.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TCounter = object
    Count: Integer;
    procedure SetCount(N: Integer);
    function GetCount: Integer;
  end;

procedure TCounter.SetCount(N: Integer);
begin
  Count := N;
end;

function TCounter.GetCount: Integer;
begin
  GetCount := Count;
end;

var
  c: TCounter;
begin
  c.SetCount(42);
  writeln(c.GetCount());
  c.SetCount(c.GetCount() + 1);
  writeln(c.GetCount());
end.

(*
CHECK:42
CHECK-NEXT:43
*)
