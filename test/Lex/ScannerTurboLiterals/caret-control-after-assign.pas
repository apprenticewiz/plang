(*
Turbo `^ctrl` control-character literal: '^' immediately followed (no gap)
by a letter, in a position where an expression -- not a type or a postfix
dereference -- is expected.  ':=' is one of the tokens next()'s
startsExpression() allow-list recognizes as unambiguously starting an
expression, so a fresh '^' right after it may begin one of these literals.
The actual code value (chr(1), non-printable) is checked at runtime by
test/Driver/Turbo/turbo-literals-*.pas instead of here, so this only
confirms it lexes as one StringLit token, not Caret+Identifier.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

x := ^A

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Assign ":="
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: StringLit
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Eof
*)
