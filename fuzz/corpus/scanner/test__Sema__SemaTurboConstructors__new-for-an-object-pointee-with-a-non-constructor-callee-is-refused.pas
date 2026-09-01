(*
Turbo Tier 5, Cluster A item 6: 'new(p, X(...))' where X names a real
method of the pointee's own ancestor chain, but not one declared
'constructor'.
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
    procedure Speak;
  end;

constructor TA.Init;
begin
end;

procedure TA.Speak;
begin
end;

var P: PA;
begin
  New(P, Speak);
end.

(*
CHECK: error: 'Speak' is not a constructor of object type 'TA'
*)
