(*
ISO §6.4.3.2 makes an array index type written as a range subject to the
same rule ISO §6.4.2.2 states for a subrange-type: both bounds must be
constants of the SAME ordinal type.  `array[1..green]` pairs an integer
lower bound with an enum upper bound; each is ordinal on its own, so the
check that only asked "is THIS bound ordinal" missed that the two bounds
disagreed with EACH OTHER, and the declaration was silently accepted
(issue #251).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: lower bound has type 'integer' but upper bound has type 'colour'
COUNT:1
*)

program p;
type
  colour = (red, green, blue);
  d = array[1..green] of char;
begin
end.
