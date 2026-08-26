(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- test.pas
module m;
  function f(x: integer): integer; begin f := x + 1 end;
end.
program p;
  import m qualified (f => g);
begin writeln(m.g(41)) end.
