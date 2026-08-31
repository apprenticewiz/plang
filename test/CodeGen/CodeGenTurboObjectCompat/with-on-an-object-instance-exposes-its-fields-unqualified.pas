(*
Turbo Tier 5, Cluster A item 7: 'with anObjectInstance do' -- reuses the
exact same with-scope mechanism (Sema::pushWithScope/CGWith.cpp) item 4
already established for the implicit 'Self' binding inside a method body,
just opened explicitly on an arbitrary object-typed expression, outside any
method.  Confirms both read and write through the unqualified names.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program WithOnObjectInstance;

type
  TAnimal = object
    Name: string[20];
    Age: integer;
  end;

var
  A: TAnimal;
begin
  with A do
  begin
    Name := 'Rex';
    Age := 5;
  end;
  writeln(A.Name, ' ', A.Age);
end.

(*
CHECK:Rex 5
*)
