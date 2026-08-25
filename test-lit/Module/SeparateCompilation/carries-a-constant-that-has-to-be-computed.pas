(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:100 97 xy
*)

//--- mod.pas
module Odd interface;
export Odd = (N, M, S);
const N = ord('a');
      M = N + 3;
      S = 'x' + 'y';
end;
end.

//--- prog.pas
program p(output);
import Odd;
begin writeln(M, ' ', N, ' ', S) end.
