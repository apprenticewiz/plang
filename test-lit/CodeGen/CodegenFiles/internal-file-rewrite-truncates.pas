(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=22
*)

program p;
var f: text; a: integer;
begin
  rewrite(f); writeln(f, 11);
  rewrite(f); writeln(f, 22);
  reset(f); read(f, a);
  writeln('a=', a)
end.
