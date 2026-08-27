(*
RUN: not %plang_ir -dump-tokens -std=iso10206 %s 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

4294967312#ff

(*
ERR: base must be
*)
