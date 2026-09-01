(*
The disambiguation this whole feature exists to get right: `type PM =
^Integer` must keep meaning "PM is a pointer-to-Integer type", never be
misread as a `^ctrl` control-character literal glued onto the identifier
`PM`.  '^Integer' has exactly the shape caretLooksLikeControlChar() looks
for (a '^' immediately followed by a letter) and would look identical to a
`^ctrl` literal from the character stream alone -- what actually keeps this
safe is that the token right before '^' here is Equal, which is
deliberately NOT on startsExpression()'s allow-list precisely because
`type X = ^T` and `const X = ^ctrl-literal` share that same token and are
not distinguishable by kind alone (see startsExpression's comment in
Scanner.cpp).  So this is a scanner-context decision, not a parser-level
"is '^' followed by a letter" rule: Caret comes out unconditionally here,
letting ParseType.cpp's existing `case TokenKind::Caret` (pointer-type
prefix) parse it exactly as before this feature existed.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

type
  PM = ^Integer;
var p: PM;

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Type "type"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "PM"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Equal "="
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Integer "Integer"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Semicolon ";"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: Var "var"
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: Identifier "p"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: Colon ":"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Identifier "PM"
CHECK-NEXT: [[P11:[0-9]+:[0-9]+]]: Semicolon ";"
CHECK-NEXT: [[P12:[0-9]+:[0-9]+]]: Eof
*)
