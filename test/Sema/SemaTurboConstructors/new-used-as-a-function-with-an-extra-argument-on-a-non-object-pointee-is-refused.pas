(*
Issue #622: unlike the statement form (whose extra argument may also be a
schema discriminant or a variant record's own case-constant), the function
form of 'New' has no confirmed real-world idiom for either of those, so an
extra argument is accepted ONLY for a pointer to an object type (a
constructor call) -- anything else is refused outright rather than
silently mishandled.
*)

(*
RUN: not %plang_ir -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  PInt = ^Integer;

var q: PInt;
begin
  q := New(PInt, 5);
end.

(*
CHECK: error: the function form of 'new' takes an extra argument only for a pointer to an object type (a constructor call), and 'integer' is not one
*)
