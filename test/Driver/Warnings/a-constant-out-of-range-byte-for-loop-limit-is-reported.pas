(*
Issue #776: the same gap as
a-constant-out-of-range-byte-assignment-is-reported.pas, but for a
for-loop bound rather than a plain assignment -- checkFor (SemaStmt.cpp)
runs warnIfConstantOutOfRange on both From and Limit (issue #654's fix),
but that checker itself only recognized an explicit subrange, so `for b :=
1 to 300 do` compiled silently for a Byte control variable even though it
already warned for a hand-written `1..255` one. Not run: under -std=turbo's
default range-checks-off setting, the loop would wrap forever rather than
trap, so only the compile-time diagnostic is checked here.
*)

(*
RUN: %plang -std=turbo %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 300 is outside the range 0..255
*)

program p;
var b: Byte;
begin
  for b := 1 to 300 do
    writeln(b)
end.
