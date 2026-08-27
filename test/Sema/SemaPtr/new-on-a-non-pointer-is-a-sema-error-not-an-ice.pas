(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'new' expects a pointer as its first argument, got 'integer'
*)

program t; var i: integer; begin new(i) end.
