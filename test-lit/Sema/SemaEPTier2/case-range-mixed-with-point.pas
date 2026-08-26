(*
RUN: %plang_ep -dump-ast %s
*)

program p; var i:integer; begin case i of 1,2: writeln; 3..9: writeln; otherwise writeln end end.
