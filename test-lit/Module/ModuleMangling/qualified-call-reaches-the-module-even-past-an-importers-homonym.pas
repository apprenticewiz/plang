(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99 1
*)

//--- test.pas
module M; function f: integer; begin f := 1 end; end.
program p(output); import M qualified;
  function f: integer; begin f := 99 end;
begin writeln(f, ' ', M.f) end.
