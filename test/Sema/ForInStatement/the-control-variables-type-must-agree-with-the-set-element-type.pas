(*
Issue #689: checkForIn used to hand the implicitly-declared loop variable
the set's own element type outright, so a DECLARED control variable's own
type was never checked against it at all -- `for c in intset do` for a
`c: color` (an enum unrelated to intset's integer elements) compiled and
ran, printing raw ordinals through a variable of the wrong type.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: loop variable 'c' has type 'color', incompatible with set element type '1..5'
*)

program p(output);
type color = (red, green, blue);
var c: color;
    intset: set of 1..5;
begin
  intset := [1, 3];
  for c in intset do writeln(ord(c))
end.
