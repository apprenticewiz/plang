(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:inner 12
CHECK-NEXT:outer 37
*)

program p(output);
var n: integer;
function outer: integer;
var i: integer;
  function inner: integer;
  begin
    inner := 12;
    outer := 37
  end;
begin i := inner; writeln('inner ', i:1) end;
begin n := outer; writeln('outer ', n:1) end.
