(*
^ctrl fragments glue in too, not just #code -- 'X' immediately followed by
^A (no gap) is one StringLit, not two.  ^A's own byte value (chr(1),
non-printable) is checked at runtime by test/Driver/Turbo instead of here.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

'X'^A

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "X
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
