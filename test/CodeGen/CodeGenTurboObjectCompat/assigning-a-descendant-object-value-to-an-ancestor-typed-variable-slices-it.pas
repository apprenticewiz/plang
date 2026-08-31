(*
Turbo Tier 5, Cluster A item 7: plain object VALUE covariance at the
statement level -- 'A := D;' for 'A: TAnimal; D: TDog;' -- confirmed
against a local fpc -Mtp build (cov1.pas) to be ordinary Pascal "object"
value slicing, not a special case: TDog's own storage starts with TAnimal's
own fields (layoutOfObject's own nested-embedding), so an ordinary
byte-range copy of A's own (smaller) size already keeps only the ancestor's
own data with no dedicated CodeGen change needed here (unlike the
by-value-PARAMETER case, which needed a real narrowing walk -- see this
item's other parameter-covariance test).
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program ObjectValueSlicingAssignment;

type
  TAnimal = object
    Name: string[20];
  end;
  TDog = object(TAnimal)
    Breed: string[20];
  end;

var
  A: TAnimal;
  D: TDog;
begin
  D.Name := 'Rex';
  D.Breed := 'Lab';
  A := D;
  writeln(A.Name);
end.

(*
CHECK:Rex
*)
