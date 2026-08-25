(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 7
*)

program p(output);
type counter = integer value 7;
     tally   = counter;
var n: counter; m: tally;
begin writeln(n, ' ', m) end.
