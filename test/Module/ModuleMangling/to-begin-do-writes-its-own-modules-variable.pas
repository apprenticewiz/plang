(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
*)

//--- test.pas
module A; var v: integer; to begin do v := 1; end.
module B; var v: integer; to begin do v := 2; end.
program p(output); import A qualified; import B qualified;
begin writeln(A.v, ' ', B.v) end.
