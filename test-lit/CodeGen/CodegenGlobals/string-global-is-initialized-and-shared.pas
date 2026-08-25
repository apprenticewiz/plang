(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: s=hello
CHECK-DAG: n=5
*)

program p;
var s: string(20); n: integer;
procedure fill;
begin s := 'hello'; n := length(s) end;
begin
  n := 0;
  fill;
  writeln('s=', s);
  writeln('n=', n)
end.
