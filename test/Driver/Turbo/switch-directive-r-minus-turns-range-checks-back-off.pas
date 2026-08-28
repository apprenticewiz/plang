(*
The other half of the position-keyed proof: a switch is not a one-way
latch.  {$R+} first (a valid index, so nothing aborts -- confirming the
'+' spelling itself is recognized, not just '-'; plain -std=turbo actually
starts UNCHECKED by default -- see the sibling {$R+} file's own comment --
so this {$R+} is what makes checking on at all from here forward, not a
restatement of the dialect's own default), then {$R-}, then an out-of-range
index that must now be
let all the way through to a normal, successful exit.  If {$R-} were not
really recorded as its own point in the table -- if, say,
dispatchSwitchDirective only ever remembered the LAST switch seen for
{$IFOPT} bookkeeping rather than building the real position-keyed table
SwitchTable.h describes -- this would still abort.  A read, not a write,
for the unchecked access, same reason as the sibling {$R+} file.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
RAN:reached the end: {$R-} silently let the bad index through
*)

program switch_r_minus;
var a: array[1..3] of integer;
    i, dummy: integer;
begin
  i := 10;
  {$R+}
  a[2] := 1;
  {$R-}
  dummy := a[i];
  writeln('reached the end: {$R-} silently let the bad index through')
end.
