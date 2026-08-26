(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:81
*)

//--- test.pas
module m interface;
  export m = (internalSquare => square);
  function internalSquare(x: integer): integer;
end.
module m;
  function internalSquare(x: integer): integer;
  begin internalSquare := x * x end;
end.
program p;
  import m;
begin writeln(square(9)) end.
