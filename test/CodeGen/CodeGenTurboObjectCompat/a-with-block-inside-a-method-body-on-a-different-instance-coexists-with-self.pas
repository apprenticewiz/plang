(*
Turbo Tier 5, Cluster A item 7: the nested case item 7's own task explicitly
calls out to test -- a 'with' block used INSIDE a method body, around a
DIFFERENT object instance than 'Self'.  pushMethodSelfScope (item 4) and
pushWithScope (this item, extended to Object) both push plain scopes onto
the SAME Symtab scope stack; the innermost (the 'with' block's own) is
consulted first, so an unqualified name the 'with' target also has would
shadow Self's, and one it does NOT have still resolves to Self's own scope
underneath.  This test uses two DIFFERENT field names (Self's own 'Name' on
TGreeter, reached explicitly as 'Self.Name' since it is shadowed by
nothing, and Other's own 'Name' on TOther, reached unqualified inside the
'with') to confirm both are reachable at once, without one clobbering the
other.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program WithInsideMethodOnAnotherInstance;

type
  TOther = object
    Name: string[20];
  end;
  TGreeter = object
    Name: string[20];
    procedure Greet(var Other: TOther);
  end;

procedure TGreeter.Greet(var Other: TOther);
begin
  with Other do
  begin
    writeln(Self.Name, ' says hi to ', Name);
  end;
end;

var
  A: TGreeter;
  B: TOther;
begin
  A.Name := 'Rex';
  B.Name := 'Fido';
  A.Greet(B);
end.

(*
CHECK:Rex says hi to Fido
*)
