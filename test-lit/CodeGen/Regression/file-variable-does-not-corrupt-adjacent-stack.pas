(*
RUN: %plang %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: 111111
CHECK-DAG: 222222
*)

program p;
var guard1: integer; f: text; guard2: integer;
begin
  guard1 := 111111; guard2 := 222222;
  rewrite(f);
  writeln(f, 'hello');
  reset(f);
  writeln(guard1);
  writeln(guard2)
end.
