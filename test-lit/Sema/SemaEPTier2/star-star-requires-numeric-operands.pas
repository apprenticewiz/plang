(*
RUN: not %plang_ep -dump-ast %s
*)

program p; var r:real; b:boolean; begin r := b ** 2.0 end.
