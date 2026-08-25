(*
RUN: %plang -g %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
CHECK:22
ERR-ABSENT-NOT: verification failed
*)

program p(output);
var x, y: integer;

procedure addone(var n: integer);
begin
  n := n + 1
end;

begin
  x := 10;
  addone(x);
  y := x * 2;
  writeln(y)
end.
