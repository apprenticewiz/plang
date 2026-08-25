(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=7 b=8
*)

//--- test.pas
program p;
var f: text; a, b: integer;
begin
  rewrite(f); writeln(f, 7, ' ', 8); reset(f);
  readln(f, a, b);
  writeln('a=', a, ' b=', b)
end.

//--- stdin.txt
111 222
