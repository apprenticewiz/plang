(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:22
*)

program p;
function outer(n: integer): integer;
  var acc: integer;
  function bump: integer;
  begin bump := acc + 1 end;
begin acc := n; outer := bump + bump end;
begin writeln(outer(10)) end.
