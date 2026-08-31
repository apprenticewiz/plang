(*
Turbo Tier 5, Cluster A item 7: parameter covariance, both mechanisms.  A
BY-VALUE ancestor-typed parameter receives only the ancestor's own
sub-object (real Pascal "object" value slicing -- confirmed against a local
fpc -Mtp build, cov3.pas) -- StringCallMarshalling::emitCallArg's new
narrowing walk GEPs through element 0 (layoutOfObject's own nested-ancestor
struct shape) until the struct type matches what the callee declared, since
handing over the whole (larger, differently-shaped) descendant struct is an
LLVM IR verifier failure, not merely a wasted copy.  A BY-VAR ancestor-typed
parameter needed no such change: a 'var' parameter is always a flat `ptr`
regardless of its declared type, and the ancestor's own fields sit at the
SAME offsets in every descendant (layoutOfObject's own nested-embedding),
so the callee's ordinary field GEPs already land in the right place without
any narrowing at all.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program ParameterCovariance;

type
  TAnimal = object
    Name: string[20];
  end;
  TDog = object(TAnimal)
    Breed: string[20];
  end;

procedure ByVal(A: TAnimal);
begin
  writeln('byval: ', A.Name);
end;
procedure ByVar(var A: TAnimal);
begin
  A.Name := 'changed';
end;

var
  D: TDog;
begin
  D.Name := 'Rex';
  D.Breed := 'Lab';
  ByVal(D);
  ByVar(D);
  writeln(D.Name, ' ', D.Breed);
end.

(*
CHECK:byval: Rex
CHECK-NEXT:changed Lab
*)
