(*
EP non-regression (Tier 2 capstone): 'a' = 'a ' is still TRUE for an EP
VarString, unaffected by anything Turbo's ShortString comparison work
added.  This is the paired EP half of
test/Driver/Turbo/shortstring-a-is-less-than-a-space-paired-with-eps-opposite-answer.pas
(same two literals, same shape of program, cross-referenced there too):
EP's `string(N)` compares by padding the SHORTER operand out to the
LONGER's own length with spaces before comparing (plang_str.cpp's strCmp,
completely separate code from Turbo's plang_sstr.cpp sstrCmp), so 'a' and
'a ' compare EQUAL -- the trailing space is padding, not real content that
would make them differ -- the OPPOSITE of Turbo's own prefix-order answer
for the identical two literals, and for an opposite reason (Turbo treats
the trailing space as real content and breaks the tie by length; EP treats
it as padding and finds no difference at all).  Booleans print lowercase
here (EP/ISO's own convention, contrast with Turbo's uppercase TRUE/FALSE),
itself a small additional confirmation that this program is genuinely
running EP's write-formatting rules, not Turbo's.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
CHECK-NEXT:false
CHECK-NEXT:false
*)

program p(output);
var
  a, b: string(10);
begin
  a := 'a';
  b := 'a ';
  writeln(a = b);
  writeln(a < b);
  writeln(a <> b)
end.
