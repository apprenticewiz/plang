(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1011
*)

program p(output);
function outer = result: integer;
  function inner: integer;
  var result: real;
  begin result := 5.5; inner := trunc(result * 2.0) end;
begin result := inner + 1000 end;
begin writeln(outer) end.
