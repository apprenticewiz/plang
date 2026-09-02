(*
Issue #637: `x in s` never range-checked its left operand against s's
declared base type under the $R+ directive -- emitSetMember (SetOps.cpp) clamped the
ordinal into the bitmask's own physical width (PlangMaxSetElements) and
just answered false for anything outside it, unlike emitSetSingleton
(the '+ [x]'/Include(s, x) path), which already range-checks against the
set's own declared base type first.  Confirmed against real fpc -Mtp,
which raises Runtime error 201 for the identical program.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 201 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 201 at $
*)

program t637;
{$R+}
var i: integer; s: set of 1..100;
begin
  i := 200;
  if i in s then writeln('unreachable: {$R+} did not trap the ''in'' operator')
end.
