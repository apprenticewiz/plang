(*
Turbo Tier 5 capstone (Cluster C item 9): TypeOf across a genuine 3-level
hierarchy (item 7's own precedent test,
test/CodeGen/CodeGenTurboObjectCompat/typeof-compares-vmt-addresses-to-
tell-instances-of-different-concrete-types-apart.pas, only ever goes 2
levels deep: TAnimal/TDog). Reuses the same TEmployee/TManager/TExecutive
shape as this directory's own main capstone test for a consistent "world"
across test/Turbo/Objects/, rather than a fresh, disconnected cast of
types.

Deliberately restricted to DIRECTLY-TYPED local variables (E: TEmployee,
M/M2: TManager, X: TExecutive) -- never through a pointer or any other
indirection where a variable's own STATIC type could differ from its
DYNAMIC one. This is not an arbitrary restriction: CGFuncCall.cpp's own
TypeOf lowering resolves purely from 'Args[0]->ResolvedType' (Sema's
STATIC type for the argument expression), by explicit design ("this
answers a purely STATIC question... Args[0] is never evaluated for its
value, only asked for its type"), so it only ever agrees with real
`fpc -Mtp` (which reads the operand's own runtime '_vptr' field, a
genuinely DYNAMIC answer) in the restricted case exercised here, where the
two coincide. See this same directory's own
typeof-through-an-ancestor-typed-pointer-answers-statically-not-
dynamically-known-gap.pas for the polymorphic case, where they do not, and
docs/turbo.md's "Documented deviations" section for the write-up.

Confirmed against a local `fpc -Mtp` build: identical output for this
exact program (plus its own leading mode-tp compatibility pragma).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program TypeOfThreeLevel;

type
  TEmployee = object
    procedure Describe; virtual;
  end;
  TManager = object(TEmployee)
    procedure Describe; virtual;
  end;
  TExecutive = object(TManager)
    procedure Describe; virtual;
  end;

procedure TEmployee.Describe; begin writeln('employee'); end;
procedure TManager.Describe; begin writeln('manager'); end;
procedure TExecutive.Describe; begin writeln('executive'); end;

var
  E: TEmployee;
  M, M2: TManager;
  X: TExecutive;
begin
  { each instance's own TypeOf matches its own type name }
  if TypeOf(E) = TypeOf(TEmployee) then writeln('E=TEmployee') else writeln('E<>TEmployee');
  if TypeOf(M) = TypeOf(TManager) then writeln('M=TManager') else writeln('M<>TManager');
  if TypeOf(X) = TypeOf(TExecutive) then writeln('X=TExecutive') else writeln('X<>TExecutive');

  { two instances of the SAME concrete type share one VMT }
  if TypeOf(M) = TypeOf(M2) then writeln('M=M2') else writeln('M<>M2');

  { every pairwise combination across the 3 levels is distinct }
  if TypeOf(E) = TypeOf(M) then writeln('E=M (WRONG)') else writeln('E<>M');
  if TypeOf(M) = TypeOf(X) then writeln('M=X (WRONG)') else writeln('M<>X');
  if TypeOf(E) = TypeOf(X) then writeln('E=X (WRONG)') else writeln('E<>X');
end.

(*
CHECK:E=TEmployee
CHECK-NEXT:M=TManager
CHECK-NEXT:X=TExecutive
CHECK-NEXT:M=M2
CHECK-NEXT:E<>M
CHECK-NEXT:M<>X
CHECK-NEXT:E<>X
*)
