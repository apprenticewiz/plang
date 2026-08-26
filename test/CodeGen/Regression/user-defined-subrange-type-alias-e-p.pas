(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p;
const Max = 10;
type SmallInt = 1..Max;
var x: SmallInt;
begin x := 3; writeln(x) end.
