(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program t; const k = 5; begin for k := 1 to 3 do writeln(1) end.

(*
CHECK: is not a variable
*)
