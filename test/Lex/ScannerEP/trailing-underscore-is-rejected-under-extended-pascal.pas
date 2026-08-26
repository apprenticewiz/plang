(*
RUN: not %plang_ir -dump-tokens -std=iso10206 %s 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

foo_

(*
ERR: begin or end
*)
