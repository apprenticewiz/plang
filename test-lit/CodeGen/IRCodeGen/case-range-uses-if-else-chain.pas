(*
RUN: %plang -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var i: integer;
begin case i of 1..5: writeln; 6..10: writeln end end.

(*
CHECK-DAG: icmp sge
CHECK-DAG: icmp sle
CHECK-NOT: switch i64
*)
