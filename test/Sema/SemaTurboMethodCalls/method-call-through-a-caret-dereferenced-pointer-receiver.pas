(*
Turbo Tier 5, Cluster A item 3: 'P^.Method(args)' -- the parser's postfix
loop applies '^' (DerefExpr) before '.' (MethodCallExpr), so by the time
Sema::checkMethodCall runs, Receiver's static type is already TAnimal
(the pointee), not ^TAnimal.  Confirmed against a local fpc -Mtp build that
the '^' may NOT be omitted here ('P.Speak' for P: ^TAnimal is "Illegal
qualifier") -- that regression case is covered separately
(implicit-pointer-receiver-is-refused-like-real-turbo-pascal.pas).
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program PointerReceiverMethodCall;

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
  P^.Speak;
  P^.Speak();
end.

(*
CHECK: (methodcall (deref P) Speak)
CHECK-NEXT: (methodcall (deref P) Speak)
*)
