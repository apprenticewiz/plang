(*
Outside -std=turbo, next()'s '^ctrl' dispatch branch is gated on
Opts.turbo() and never even evaluated, so '^' always falls straight to
scanSymbol's existing case -- Caret -- regardless of what follows it or
what token came before it.  Default dialect is ISO 7185.
*)

(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

x := ^M

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Assign ":="
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "M"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Eof
*)
