(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.5 -7 * true hello world
*)

//--- mod.pas
module Kinds interface;
export Kinds = (Ratio, Neg, Star, Yes, Big);
const Ratio = 3.5;
      Neg = -7;
      Star = '*';
      Yes = true;
      Big = 'hello world';
end;
end.

//--- prog.pas
program p(output);
import Kinds;
begin writeln(Ratio:0:1, ' ', Neg, ' ', Star, ' ', Yes, ' ', Big) end.
