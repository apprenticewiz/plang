(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 12
*)

//--- test.pas
module m;
  function f(x: integer): integer; begin f := x + 1 end;
  function g(x: integer): integer; begin g := x + 2 end;
end.
program p;
  import m (f => plus1);
begin writeln(plus1(10), ' ', g(10)) end.
