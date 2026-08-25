(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:21
*)

//--- test.pas
module M;
  function outer(x: integer): integer;
    function inner(y: integer): integer; begin inner := y * x end;
  begin outer := inner(3) end;
end.
program p;
  import M;
begin writeln(outer(7)) end.
