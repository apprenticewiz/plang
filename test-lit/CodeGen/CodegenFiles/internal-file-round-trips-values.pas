(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=42 b=77
*)

program p;
var f: text; a, b: integer;
begin
  rewrite(f);
  writeln(f, 42);
  writeln(f, 77);
  reset(f);
  read(f, a);
  read(f, b);
  writeln('a=', a, ' b=', b)
end.
