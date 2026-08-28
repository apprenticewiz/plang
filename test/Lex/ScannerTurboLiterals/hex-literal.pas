(*
Turbo `$hex` integer literal: a '$' followed by one or more hex digits.
scanHexLiteral converts to a decimal Lexeme, same as EP's own `16#FF`
nondecimal literal does, so IntLit downstream never has to know which
dialect's spelling produced it.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

$FF

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "255"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
