(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p(output);
var c: char;
begin for c := 1 to 10 do writeln(c) end.

(*
CHECK: incompatible with control variable 'c'
*)
