(*
CGProcCall's Inc/Dec lowering widens the target's current value to i64
(add/subtract, range-check, narrow back) through ToI64's own signedness-
blind single-argument overload -- the same pre-ladder "i8 always means
Char/Boolean, so zero-extend" assumption CGAssign's plain-assignment path
used to make (see shortint-assigned-to-a-wider-signed-integer-sign-
extends-issue-177.pas).  A negative ShortInt's zero-extended bit pattern
(-100 read as 156) then failed Inc's own in-range-value range check
spuriously: `{$R+} s: ShortInt; s := -100; Inc(s)` reported "Runtime error
201: Range check error" for a perfectly legal -100 -> -99 step (issue
#177).  Fixed by widening with x's own Sema-resolved signedness
(exprIsSigned, OrdinalSignedness.h) instead.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before=-100
CHECK-NEXT:after=-99
CHECK-NEXT:back=-100
*)

{$R+}
program p;
var s: ShortInt;
begin
  s := -100;
  writeln('before=', s);
  Inc(s);
  writeln('after=', s);
  Dec(s);
  writeln('back=', s)
end.
