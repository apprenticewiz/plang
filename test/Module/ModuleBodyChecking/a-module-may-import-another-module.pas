(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

//--- test.pas
module A;
  function one: integer; begin one := 1 end;
end.
module B;
  import A;
  function two: integer; begin two := one + 1 end;
end.
program p;
  import B;
begin writeln(two) end.
