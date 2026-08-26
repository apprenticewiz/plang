(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
var g: file of integer; v: integer;
procedure q(protected var f: file of integer; var x: integer);
begin read(f, x) end;
begin
  rewrite(g); write(g, 42); reset(g);
  q(g, v);
  writeln(v:1)
end.
