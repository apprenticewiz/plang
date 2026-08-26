(*
RUN: %plang_ep -dump-ast %s
*)

program p; var i:integer; begin case i of 1..5: writeln; 6..10: writeln end end.
