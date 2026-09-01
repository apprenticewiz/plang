(*
Turbo Pascal gives '@' a job ISO 7185/EP never do: a prefix address-of
operator, unrelated to '^' (postfix dereference / a pointer type's prefix
marker).  Scanner.cpp's '@' dispatch is the only place that decides between
the two -- under -std=turbo it hands back the new At token instead of
folding '@' into Caret the way every other dialect still does (see
test/Lex/ScannerLexicalAlternatives/at-sign-is-the-arrow.pas, unchanged by
this and still tokenizing the same input as plain Caret).

RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

p := @ q; r := q^

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "p"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Assign
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: At "@"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "q"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Semicolon
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Identifier "r"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: Assign
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: Identifier "q"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Eof
*)
