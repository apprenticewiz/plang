(*
Turbo Tier 5, Cluster A item 6: 'dispose(p, X)' where X names a real
method of the pointee's own ancestor chain, but not one declared
'destructor' -- err_new_init_not_constructor's mirror.
*)

(*
RUN: not %plang_ir -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  PA = ^TA;
  TA = object
    constructor Init;
  end;

constructor TA.Init;
begin
end;

var P: PA;
begin
  Dispose(P, Init);
end.

(*
CHECK: error: 'Init' is not a destructor of object type 'TA'
*)
