(*
The 14 words TokenKinds.def reserves only for -std=turbo, all recognized as
keywords there -- one shared source rather than one file per word, since
each scans independently regardless of what surrounds it (the same
rationale ScannerEP/ep-keywords-recognized.pas uses for its own list).

RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

asm constructor destructor exports implementation inherited inline library object shl shr unit uses xor

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Asm "asm"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Constructor "constructor"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Destructor "destructor"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Exports "exports"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Implementation "implementation"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Inherited "inherited"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: Inline "inline"
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: Library "library"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: Object "object"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Shl "shl"
CHECK-NEXT: [[P11:[0-9]+:[0-9]+]]: Shr "shr"
CHECK-NEXT: [[P12:[0-9]+:[0-9]+]]: Unit "unit"
CHECK-NEXT: [[P13:[0-9]+:[0-9]+]]: Uses "uses"
CHECK-NEXT: [[P14:[0-9]+:[0-9]+]]: Xor "xor"
*)
