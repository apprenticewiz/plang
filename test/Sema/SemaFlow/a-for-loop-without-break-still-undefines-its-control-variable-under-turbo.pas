(*
Regression gate for break-leaves-the-for-loops-control-variable-readable.pas:
the carve-out is keyed on FlowLoopBroke_ actually having seen a Break in
THIS loop's own body, not on the dialect being Turbo -- an ordinary for-loop
with no Break in it must still warn under -std=turbo exactly the way it
always has (see the-control-variable-is-undefined-after-its-for.pas,
test/Driver/Warnings, for the same check under the default dialect).
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'i' is undefined here: a for-statement leaves its control variable undefined when it finishes
*)

program p;
var i: Integer;
begin
  for i := 1 to 10 do
    writeln(i);
  writeln('after: ', i)
end.
