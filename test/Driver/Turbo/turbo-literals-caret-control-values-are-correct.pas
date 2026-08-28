(*
Runtime companion to test/Lex/ScannerTurboLiterals/caret-control-*.pas:
checks the actual ordinal values `^ctrl` literals produce, including that
the letter is case-insensitive (^A and ^a both give 1) and that the
alphabet-position rule holds at both ends (^A = 1, ^Z = 26) and for the two
control characters real Turbo Pascal code names this way constantly, CR and
LF (^M = 13, ^J = 10).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:1
CHECK-NEXT:26
CHECK-NEXT:13
CHECK-NEXT:10
*)

program p;
begin
  writeln(ord(^A));
  writeln(ord(^a));
  writeln(ord(^Z));
  writeln(ord(^M));
  writeln(ord(^J))
end.
