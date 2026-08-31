(*
Turbo Tier 5, Cluster A item 6: 'Fail' used outside a constructor's own
body -- confirmed against a local fpc -Mtp build that 'Fail' is not even a
recognized identifier anywhere else ("Identifier not found"); plang gives
it a dedicated diagnostic instead, matching the same "context-restricted
required identifier" treatment err_inherited_outside_method already gives
'inherited'.  Exercised both at ordinary program scope and inside an
ordinary (non-constructor) method.
*)

(*
RUN: not %plang_ir -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  TA = object
    procedure Ordinary;
  end;

procedure TA.Ordinary;
begin
  Fail;
end;

begin
  Fail;
end.

(*
CHECK: error: 'Fail' may only be used inside a constructor
CHECK: error: 'Fail' may only be used inside a constructor
*)
