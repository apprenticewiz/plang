(*
The same Turbo-only reserved words are plain identifiers under Extended
Pascal too: DIALECT_KEYWORD's mask names D_Turbo alone for every one of
them, so D_ISO10206 never sees them as keywords.  A second, deliberately
separate check from turbo-keywords-are-identifiers-in7185.pas's ISO 7185
one -- the widened per-dialect mechanism (TokenKinds.def's
DIALECT_KEYWORD/Scanner.cpp's keywordDialects) answers this per dialect, not
via one "is it Extended Pascal" boolean, and this is what actually proves
that.

RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

asm constructor destructor exports implementation inherited inline library object shl shr unit uses xor

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "asm"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "constructor"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier "destructor"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "exports"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Identifier "implementation"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Identifier "inherited"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: Identifier "inline"
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: Identifier "library"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: Identifier "object"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Identifier "shl"
CHECK-NEXT: [[P11:[0-9]+:[0-9]+]]: Identifier "shr"
CHECK-NEXT: [[P12:[0-9]+:[0-9]+]]: Identifier "unit"
CHECK-NEXT: [[P13:[0-9]+:[0-9]+]]: Identifier "uses"
CHECK-NEXT: [[P14:[0-9]+:[0-9]+]]: Identifier "xor"
*)
