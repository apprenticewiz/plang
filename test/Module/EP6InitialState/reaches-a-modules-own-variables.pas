(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 8
*)

//--- test.pas
module V interface;
export V = (Counter, Tally);
var Counter: integer value 5;
type k = integer value 8;
var Tally: k;
end;
end.
program p(output);
import V;
begin writeln(Counter, ' ', Tally) end.
