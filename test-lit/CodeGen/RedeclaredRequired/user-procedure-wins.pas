(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:mine 7
*)

program p(output);
procedure pack(n: integer);
begin writeln('mine ', n:1) end;
begin pack(7) end.
