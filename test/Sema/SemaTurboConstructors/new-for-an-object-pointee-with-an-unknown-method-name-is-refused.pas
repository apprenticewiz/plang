(*
Turbo Tier 5, Cluster A item 6: 'new(p, Bogus)' where Bogus names no
method anywhere in the pointee's own ancestor chain.
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
  New(P, Bogus);
end.

(*
CHECK: error: object type 'TA' has no method 'Bogus'
*)
