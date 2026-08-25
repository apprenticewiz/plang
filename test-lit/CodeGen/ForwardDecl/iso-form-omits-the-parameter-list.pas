(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:210.
*)

program p;
procedure b(n: integer); forward;
procedure a(n: integer); begin if n > 0 then b(n - 1) end;
procedure b;
begin write(n); a(n) end;
begin a(3); writeln('.') end.
