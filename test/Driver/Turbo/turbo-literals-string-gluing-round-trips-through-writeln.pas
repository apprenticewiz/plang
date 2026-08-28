(*
Runtime companion to test/Lex/ScannerTurboLiterals/string-gluing-*.pas: the
task's own motivating example, 'AB'#13#10'CD', glues into a single
6-character StringLit at the lexical level (A, B, CR, LF, C, D) and must
come back out through writeln as exactly those six bytes, not four separate
pieces needing a parser-level '+'.  Piped through od -c during verification
(not here -- FileCheck can match the raw \r\n directly) confirmed the exact
byte sequence 'A' 'B' '\r' '\n' 'C' 'D' with nothing extra and nothing
missing.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:AB
CHECK-NEXT:CD
*)

program p;
begin
  write('AB'#13#10'CD')
end.
