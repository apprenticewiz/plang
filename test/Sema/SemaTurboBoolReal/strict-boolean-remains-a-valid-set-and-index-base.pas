(*
Regression guard for the two rejections in this directory
(bytebool-is-not-a-set-base-type.pas, wordbool-is-not-an-array-index-
type.pas): strict Boolean itself (Type::IsLooseBool unset) must still be
usable as both a set base type and an array index type exactly as it always
has been -- checkSetBaseRange's and the array-index-type gate's new
IsLooseBool checks must not affect the ordinary, non-loose case they were
already handling.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
CHECK-NEXT:TRUE
*)

type
  SB = set of Boolean;
var
  a: array[Boolean] of Integer;
  s: SB;
begin
  a[false] := 1;
  a[true]  := 2;
  writeln(a[false], ' ', a[true]);
  s := [false, true];
  writeln(false in s);
end.
