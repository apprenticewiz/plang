(*
The same Turbo-only reserved words as turbo-keywords-recognized.pas must be
plain identifiers under the default (ISO 7185) dialect: none of them is a
letter/digit-only word ISO 7185 or ISO 10206 reserves, so a conforming
program in either dialect is entitled to use one as a name (verified
separately, under ISO 10206, by
turbo-keywords-are-identifiers-in-iso10206.pas).

RUN: %plang_ir -dump-tokens %s | FileCheck %s
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
