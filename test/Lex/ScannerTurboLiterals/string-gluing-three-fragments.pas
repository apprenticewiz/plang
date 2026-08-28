(*
Lexical string gluing (Scanner.cpp's scanString): adjacent string / #code /
^ctrl fragments with no separator between them concatenate into ONE
StringLit token at the lexical level, not three tokens needing a
parser-level '+'.  #65 = 'A', so 'AB' + #65 + 'CD' glues into "ABACD" --
fully printable, so (unlike a glued run that includes a CR/LF/other C0
control byte -- see test/Driver/Turbo for those) the exact content can be
asserted directly here.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

'AB'#65'CD'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "ABACD"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
