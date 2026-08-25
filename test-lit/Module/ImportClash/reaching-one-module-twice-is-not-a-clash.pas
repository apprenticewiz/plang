(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
*)

//--- test.pas
module A; function f: integer; begin f := 1 end; end.
module B; import A; function g: integer; begin g := f + 1 end; end.
program p(output); import A; import B;
begin writeln(f, ' ', g) end.
