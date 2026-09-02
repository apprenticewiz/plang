(*
The negative half of caret-control-after-of-in-case-statement.pas's fix
(issue #600): `array of`/`set of`/`file of`'s own 'of' introduces an
ELEMENT TYPE, never a case-label, so it must keep meaning Caret
(pointer-type prefix) the way it always has -- Parser::parseVariantPart
and Parser::parseCaseStmt are the only two call sites that arm
Scanner::allowCaretControlCharNext() before consuming 'of', and neither
of the array/set/file type-denoter productions (ParseType.cpp) does.
'^Integer' has exactly the shape caretLooksLikeControlChar() looks for (a
'^' immediately followed by a letter) and would look identical to a
`^ctrl` literal from the character stream alone if this were ever
mishandled.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

type
  A = array[1..3] of ^Integer;
  S = set of ^Integer;
  F = file of ^Integer;

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Type "type"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "A"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Equal "="
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Array "array"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: LeftBracket "["
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: IntLit "1"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: DotDot ".."
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: IntLit "3"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: RightBracket "]"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Of "of"
CHECK-NEXT: [[P11:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P12:[0-9]+:[0-9]+]]: Integer "Integer"
CHECK-NEXT: [[P13:[0-9]+:[0-9]+]]: Semicolon ";"
CHECK-NEXT: [[P14:[0-9]+:[0-9]+]]: Identifier "S"
CHECK-NEXT: [[P15:[0-9]+:[0-9]+]]: Equal "="
CHECK-NEXT: [[P16:[0-9]+:[0-9]+]]: Set "set"
CHECK-NEXT: [[P17:[0-9]+:[0-9]+]]: Of "of"
CHECK-NEXT: [[P18:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P19:[0-9]+:[0-9]+]]: Integer "Integer"
CHECK-NEXT: [[P20:[0-9]+:[0-9]+]]: Semicolon ";"
CHECK-NEXT: [[P21:[0-9]+:[0-9]+]]: Identifier "F"
CHECK-NEXT: [[P22:[0-9]+:[0-9]+]]: Equal "="
CHECK-NEXT: [[P23:[0-9]+:[0-9]+]]: File "file"
CHECK-NEXT: [[P24:[0-9]+:[0-9]+]]: Of "of"
CHECK-NEXT: [[P25:[0-9]+:[0-9]+]]: Caret "^"
CHECK-NEXT: [[P26:[0-9]+:[0-9]+]]: Integer "Integer"
CHECK-NEXT: [[P27:[0-9]+:[0-9]+]]: Semicolon ";"
CHECK-NEXT: [[P28:[0-9]+:[0-9]+]]: Eof
*)
