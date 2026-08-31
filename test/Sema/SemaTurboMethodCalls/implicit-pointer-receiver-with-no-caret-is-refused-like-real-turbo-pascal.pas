(*
Turbo Tier 5, Cluster A item 3: confirmed against a local fpc -Mtp build
("Illegal qualifier") that 'P.Speak' for P: ^TAnimal is refused -- the '^'
may not be omitted for a pointer receiver, unlike some object models that
auto-dereference.  Without the '^', checkExpr(Receiver) reports the
POINTER type itself (never unwrapped to its pointee), so
err_method_call_receiver_not_object fires exactly the way it would for any
other non-object receiver.
*)

(*
RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program ImplicitPointerReceiverRefused;

type
  TAnimal = object
    procedure Speak;
  end;
  PAnimal = ^TAnimal;

procedure TAnimal.Speak;
begin
end;

var
  P: PAnimal;
begin
  New(P);
  P.Speak;
end.

(*
CHECK: error: '^TAnimal' is not an object; '.' followed by '(' is only a method call
*)
