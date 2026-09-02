(*
Issue #622: checkNewInitExpr's own sibling of new-for-an-object-pointee-
with-an-unknown-method-name-is-refused.pas, for the FUNCTION form --
'q := New(PA, Bogus)' where Bogus names no method anywhere in the
pointee's own ancestor chain.
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

var q: PA;
begin
  q := New(PA, Bogus);
end.

(*
CHECK: error: object type 'TA' has no method 'Bogus'
*)
