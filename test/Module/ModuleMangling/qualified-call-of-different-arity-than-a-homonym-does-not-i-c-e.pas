(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 10
*)

//--- test.pas
module M; function f(x: integer): integer; begin f := x * 2 end; end.
program p(output); import M qualified;
  function f: integer; begin f := 7 end;
begin writeln(f, ' ', M.f(5)) end.
