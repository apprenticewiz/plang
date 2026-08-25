(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK-DAG: e
*)

program p(output);
var r: real;
begin r := 3.14159; write(r:10:-1); writeln end.
