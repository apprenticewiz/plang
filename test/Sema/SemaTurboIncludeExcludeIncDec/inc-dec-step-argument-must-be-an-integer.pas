(*
Inc(x, n)/Dec(x, n)'s optional second argument is a step count, always
integer regardless of x's own type -- the identical rule EP §6.7.6.5's
succ/pred second argument already follows, and the identical diagnostic
(err_step_argument_not_integer) reused rather than a near-duplicate.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: the second argument of 'inc' must be an integer, got 'char'
CHECK: the second argument of 'dec' must be an integer, got 'char'
*)

program p;
var
  i: Integer;
begin
  i := 0;
  Inc(i, 'a');
  Dec(i, 'a');
end.
