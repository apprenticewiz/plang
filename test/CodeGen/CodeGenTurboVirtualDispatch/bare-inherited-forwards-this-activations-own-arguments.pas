(*
Turbo Tier 5, Cluster A item 5: the bare 'inherited;' form (no method name,
no argument list) means "the same method this activation itself overrides,
called with the same arguments this activation itself received" --
CodeGen forwards the CURRENTLY EXECUTING function's own LLVM parameters
(getArg(1) onward, skipping Self) unchanged rather than re-marshalling
anything (CGProcCall::emitInheritedCallStmt), which is sound only because
an override's parameter list is required to match its ancestor's exactly.
TDog.SetName takes the same 'N: string' TAnimal.SetName does and forwards it
untouched via bare 'inherited;', then adds its own behavior after -- proof
that the forwarded value is the CALLER's real argument, not a stale or
default one.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TAnimal = object
    Name: string[20];
    procedure SetName(N: string); virtual;
  end;
  TDog = object(TAnimal)
    procedure SetName(N: string); virtual;
  end;

procedure TAnimal.SetName(N: string);
begin
  Name := N;
end;

procedure TDog.SetName(N: string);
begin
  inherited;
  writeln('TDog got: ', Name);
end;

var
  D: TDog;
begin
  D.SetName('Rex');
end.

(*
CHECK:TDog got: Rex
*)
