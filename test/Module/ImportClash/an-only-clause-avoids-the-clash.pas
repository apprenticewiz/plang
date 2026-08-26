(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9 2
*)

//--- test.pas
module A; function f: integer; begin f := 1 end;
 function h: integer; begin h := 9 end; end.
module B; function f: integer; begin f := 2 end; end.
program p(output); import A only h; import B;
begin writeln(h, ' ', f) end.
