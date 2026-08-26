(*
RUN: %plang_ep -dump-ast %s
*)

program p; const Base = 10; type T = 1..10; var x: T; const Lim = Base * 2; var y: integer; begin x := 5; y := Lim end.
