(*
ISO §6.7.1's set-constructor member-range production is the same
`lo..hi` range subject to ISO §6.4.2.2's rule that both bounds be
constants of the SAME ordinal type -- the rule issue #251 restored for a
subrange-type's and an array index-range's bound pair, in SemaType.cpp.
checkSetLit's own range handling has the identical "whichever bound is
ordinal" pattern #251 fixed elsewhere: `Et = Lo->isOrdinal() ? Lo : Hi`
never asks whether the OTHER bound agrees, so `[1..'z']` (integer lower
bound, char upper bound; each ordinal on its own) was silently accepted
as a set of integer, with the char bound's disagreement going unheard
(issue #395).

RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: lower bound has type 'integer' but upper bound has type 'char'
COUNT:1
*)

program p;
var x : integer; b : boolean;
begin b := x in [1..'z'] end.
