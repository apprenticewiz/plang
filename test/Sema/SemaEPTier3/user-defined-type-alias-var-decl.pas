(*
RUN: %plang -dump-ast %s
*)

program p; type MyInt = 1..100; var x:MyInt; begin x := 42 end.
