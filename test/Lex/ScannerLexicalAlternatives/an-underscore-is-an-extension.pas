(*
RUN: not %plang_ir -dump-tokens %s 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

my_var

(*
ERR: underscore
*)
