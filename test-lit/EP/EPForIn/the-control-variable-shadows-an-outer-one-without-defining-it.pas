(*
The control variable is a FRESH one for the body's duration, so its
assignment must not escape: an outer variable of the same spelling is no
more assigned after the loop than it was before, and reading it still
warns.  Marking it assigned unconditionally would have traded a false
positive for a false negative.

This one passes against the parent commit too -- it guards the FIX from
being over-applied rather than catching the original defect, which is
what a guard is for.  The exit path is deliberately not asserted on
(the original test doesn't either); only that the diagnostic appears.

RUN: %plang_ep %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: before it has been given a value
*)

program p(output);
var c: char;
begin for c in ['a'..'c'] do write(c); writeln(c) end.
