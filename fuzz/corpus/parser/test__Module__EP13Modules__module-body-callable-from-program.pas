(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- test.pas
module Arith;
  function double(x: integer): integer;
  begin double := x * 2 end;
end.
program p;
  import Arith;
var n: integer;
begin
  n := double(21);
  writeln(n)
end.
