(*
ISO §6.4.2.2 requires a subrange-type's two bounds to be constants of the
SAME ordinal type, which becomes the subrange's host type.  resolveTypeImpl
picked that host type as "whichever bound is ordinal" -- the low bound if
it qualified, else the high one -- and never asked whether the OTHER bound
agreed, so a char lower bound paired with an integer upper bound (each
ordinal on its own, disagreeing with each other) was silently accepted
(issue #251).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: lower bound has type 'char' but upper bound has type 'integer'
COUNT:1
*)

program p;
type
  c = 'a'..100;
begin
end.
