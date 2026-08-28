(*
Same as caret-control-not-recognized-outside-turbo.pas, under Extended
Pascal: still gated on Opts.turbo(), still unaffected.
*)

(*
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

x := ^M

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Assign ":="
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "M"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Eof
*)
