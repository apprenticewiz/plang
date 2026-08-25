(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:100 97
*)

//--- test.pas
module Odd interface;
export Odd = (N, M);
const N = ord('a');
      M = N + 3;
end;
end.
program p(output);
import Odd;
begin writeln(M, ' ', N) end.
