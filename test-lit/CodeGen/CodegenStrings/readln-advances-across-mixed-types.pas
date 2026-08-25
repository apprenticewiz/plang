(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:s=first line t=second n=99
*)

//--- test.pas
program p;
var s, t: string(20); n: integer;
begin
  readln(s); readln(t); readln(n);
  writeln('s=', s, ' t=', t, ' n=', n)
end.

//--- stdin.txt
first line
second
99
