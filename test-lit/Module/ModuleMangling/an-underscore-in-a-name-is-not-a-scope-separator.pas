(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
*)

//--- test.pas
module a; function b: integer; begin b := 1 end; end.
program p(output); import a;
  function a_b: integer; begin a_b := 2 end;
begin writeln(b); writeln(a_b) end.
