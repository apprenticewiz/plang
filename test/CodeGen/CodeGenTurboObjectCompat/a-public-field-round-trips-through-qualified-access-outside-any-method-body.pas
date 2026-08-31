(*
Turbo Tier 5, Cluster A item 7: 'A.Field'/'P^.Field' for an object type,
OUTSIDE any method body -- the second confirmed pre-existing gap this item
fixes.  Before this item, Sema::checkField and CodeGen's own field-access
emission (CGFieldAccess.cpp) both hard-restricted to TypeKind::Record; an
object's own fields live in the exact same RecordFields list (see its own
comment, Type.h), so this reuses the record machinery directly rather than
adding a parallel implementation.  Round-trips a value through both the
plain-variable ('A.Field') and pointer-dereference ('P^.Field') spellings.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program ExternalFieldAccess;

type
  TAnimal = object
    Name: string[20];
    Age: integer;
  end;

var
  A: TAnimal;
  PA: ^TAnimal;
begin
  A.Name := 'Rex';
  A.Age := 5;
  writeln(A.Name, ' ', A.Age);
  PA := @A;
  PA^.Age := 6;
  writeln(A.Name, ' ', A.Age);
end.

(*
CHECK:Rex 5
CHECK-NEXT:Rex 6
*)
