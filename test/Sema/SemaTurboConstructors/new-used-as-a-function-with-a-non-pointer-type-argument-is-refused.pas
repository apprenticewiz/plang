(*
Issue #622: the function form of 'New' ('p := New(PtrType[, Ctor[(args)])')
requires a POINTER TYPE identifier as its first argument, never the
pointee type alone -- confirmed against a local fpc -Mtp build: 'New(TFoo)'
for an unpointered object type is refused, "pointer type expected".
*)

(*
RUN: not %plang_ir -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  TFoo = object
    x: Integer;
  end;
  PFoo = ^TFoo;

var q: PFoo;
begin
  q := New(TFoo);
end.

(*
CHECK: error: the function form of 'new' expects a pointer type as its argument, got 'TFoo'
*)
