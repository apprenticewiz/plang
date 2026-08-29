(*
Include(s, x) / Exclude(s, x)'s second argument must be assignment-
compatible with s's own declared base type -- the identical requirement
checkSetLit's typed-constructor arm already asks of each member of a
literal `[x]` (err_assign_mismatch, reused rather than a second
near-identical diagnostic).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign 'char' to variable of type 'TColor'
*)

program p;
type
  TColor = (Red, Green, Blue);
  TColorSet = set of TColor;
var
  s: TColorSet;
begin
  s := [Red];
  Include(s, 'x');
end.
