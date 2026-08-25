(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 200
*)

//--- test.pas
module A; var v: integer;
  procedure helper; begin v := v + 10 end;
  procedure bump; begin helper end;
  function get: integer; begin get := v end; end.
module B; var v: integer;
  procedure helper; begin v := v + 100 end;
  procedure bump; begin helper end;
  function get: integer; begin get := v end; end.
program p(output); import A qualified; import B qualified;
begin A.bump; B.bump; B.bump; writeln(A.get, ' ', B.get) end.
