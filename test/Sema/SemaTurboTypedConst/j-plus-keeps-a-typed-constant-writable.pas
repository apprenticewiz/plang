(*
Issue #603's default-on sibling: {$J+} is the default (Switch::WritableConst,
CompilerSwitches.def), and a typed constant with no {$J-} in force stays
writable, matching TP7/FPC -Mtp's usual "typed constant is a preinitialized
variable" behavior -- see typed-constant-persists-across-calls.pas in this
directory for that behavior's other regression. This is the positive
control for j-minus-makes-a-typed-constant-immutable.pas: same program
shape, no {$J-}, and it must still compile, run, and show the write took
effect.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:2
*)

program p;
const x: integer = 1;
begin
  x := 2;
  writeln(x)
end.
