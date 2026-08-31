(*
Turbo Tier 5, Cluster A item 3: 'Obj.Method(args)' on a directly-declared
object type (no inheritance involved) resolves through Sema::checkMethodCall
with no diagnostic -- the base case the ancestor-chain walk builds on.
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program SimpleMethodCall;

type
  TAnimal = object
    procedure Speak(Times: Integer);
  end;

procedure TAnimal.Speak(Times: Integer);
begin
end;

var
  A: TAnimal;
begin
  A.Speak(3);
end.

(*
CHECK: (methodcall A Speak 3)
*)
