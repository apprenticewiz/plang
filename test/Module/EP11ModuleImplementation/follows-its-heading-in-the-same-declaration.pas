(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
CHECK-NEXT:1
*)

//--- test.pas
module Arith interface;
export Arith = (Twice, Counter);
var Counter: integer;
function Twice(x: integer): integer;
end;
function Twice;
begin Counter := Counter + 1; Twice := x + x end;
to begin do Counter := 0;
end.
program p(output);
import Arith;
begin writeln(Twice(21)); writeln(Counter) end.
