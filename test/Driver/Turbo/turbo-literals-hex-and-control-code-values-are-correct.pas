(*
Runtime companion to test/Lex/ScannerTurboLiterals/, whose dump-tokens
fixtures check token *shape* only for anything whose decoded content is a
non-printable control byte (see e.g. control-code-boundary-value-255.pas's
own comment on why).  This one actually runs the compiled program and
checks the values that come out: $hex's IntLit value, #code's decimal and
hex spellings, and #code round-tripping through ord() at the byte's own
value (0 and 255, the ends of Char's range).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:255
CHECK-NEXT:65
CHECK-NEXT:65
CHECK-NEXT:0
CHECK-NEXT:255
*)

program p;
begin
  writeln($FF);
  writeln(ord(#65));
  writeln(ord(#$41));
  writeln(ord(#0));
  writeln(ord(#255))
end.
