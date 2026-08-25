(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p(output);
function outer: integer;
var i: integer;
  function inner: integer;
  begin inner := 1; outer := 5 end;
begin i := inner end;
begin writeln(outer:1) end.
