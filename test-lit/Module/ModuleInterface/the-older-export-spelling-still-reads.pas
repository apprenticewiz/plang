(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
*)

//--- test.pas
module v interface;
  export function scale(x: integer; k: integer): integer;
end.
module v;
  function scale(x: integer; k: integer): integer;
  begin scale := x * k end;
end.
program p;
  import v;
begin writeln(scale(2, 3)) end.
