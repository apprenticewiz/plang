(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4
*)

program p;
procedure outer;
  var k: integer;
  procedure b(n: integer); forward;
  procedure a(n: integer); begin if n > 0 then b(n - 1) end;
  procedure b(n: integer); begin k := k + 1; a(n) end;
begin k := 0; a(4); writeln(k) end;
begin outer end.
