(*
The critical negative case for PrevKind's allow-list (Scanner.h/.cpp): '^M'
has exactly the shape caretLooksLikeControlChar() looks for (a '^'
immediately followed by a letter), but the token right before it here is an
Identifier, which is NOT on startsExpression()'s allow-list -- Identifier is
how a designator ends, so a '^' straight after one is always the existing
postfix-dereference Caret (`p^`), never the start of a new literal.  Proves
the scanner is deciding this from token-stream context, not merely "is '^'
followed by a letter" -- the same rule that keeps
caret-control-in-type-declaration-not-misparsed.pas correct.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

p^M

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "p"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier "M"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Eof
*)
