(*
Turbo Tier 5, Cluster A item 7: TypeOf(x) -- confirmed against a local fpc
-Mtp build (typeof1.pas) that TypeOf answers the generic 'Pointer' type
(the address of x's own object type's VMT global) and is compared with '='
to ask "is this really a TDog" -- TypeOf(A) <> TypeOf(D) for an ancestor
instance and a descendant instance, TypeOf(D) = TypeOf(D2) for two separate
instances of the SAME concrete type, and TypeOf(anInstance) = TypeOf(aType
name) both name the one VMT global.  Reuses getOrCreateVmt's existing
per-type memoized global exactly as item 5/6 built it.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program TypeOfComparesVmtAddresses;

type
  TAnimal = object
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;

procedure TAnimal.Speak; begin writeln('animal'); end;
procedure TDog.Speak; begin writeln('dog'); end;

var
  A: TAnimal;
  D, D2: TDog;
begin
  if TypeOf(A) = TypeOf(D) then writeln('A=D') else writeln('A<>D');
  if TypeOf(D) = TypeOf(D2) then writeln('D=D2') else writeln('D<>D2');
  if TypeOf(D) = TypeOf(TDog) then writeln('D=TDog') else writeln('D<>TDog');
end.

(*
CHECK:A<>D
CHECK-NEXT:D=D2
CHECK-NEXT:D=TDog
*)
