(*
RUN: %plang_ep -dump-ast %s
*)

program p; var i:integer; begin case i of 1: writeln; else writeln end end.
