(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4
CHECK-NEXT:3
CHECK-NEXT:2
CHECK-NEXT:999
*)

program p(output);
var f: file [1..5] of integer; v, pos1, lp: integer;
begin rewrite(f);
  v := 1; write(f, v); v := 2; write(f, v); v := 3; write(f, v);
  pos1 := position(f); writeln(pos1:1);
  lp := lastposition(f); writeln(lp:1);
  SeekRead(f, 2); read(f, v); writeln(v:1);
  SeekWrite(f, 1); v := 999; write(f, v);
  SeekRead(f, 1); read(f, v); writeln(v:1)
end.
