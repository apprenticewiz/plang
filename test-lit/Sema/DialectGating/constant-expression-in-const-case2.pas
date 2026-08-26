(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
const c = 1 < 2;
begin writeln(c) end.

(*
CHECK: expected ';', got '<'
*)
