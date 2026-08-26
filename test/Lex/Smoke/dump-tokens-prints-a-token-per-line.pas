(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

program test;
var x : integer;
begin
  x := 42
end.

(*
-dump-tokens is Scanner-only -- no Parser, no Sema -- proving the wiring
that test-lit/Lex/'s real scanner_test.cpp migration will build on.  Each
line is "<line>:<col>: KindName "lexeme"".  This CHECK block pins exact
source line numbers (empirically confirmed against a real run, not hand-
counted), so keep the RUN block above minimal -- reflowing it shifts
every line below.  Eof itself is not pinned to an exact line: the
scanner keeps going past "end." through this trailing comment to the
real end of the file, so its line depends on how long this comment is --
only its column (1, nothing else on that line) and empty lexeme matter.

CHECK: 5:1: Program "program"
CHECK-NEXT: 5:9: Identifier "test"
CHECK-NEXT: 5:13: Semicolon ";"
CHECK-NEXT: 6:1: Var "var"
CHECK-NEXT: 6:5: Identifier "x"
CHECK-NEXT: 6:7: Colon ":"
CHECK-NEXT: 6:9: Integer "integer"
CHECK-NEXT: 6:16: Semicolon ";"
CHECK-NEXT: 7:1: Begin "begin"
CHECK-NEXT: 8:3: Identifier "x"
CHECK-NEXT: 8:5: Assign ":="
CHECK-NEXT: 8:8: IntLit "42"
CHECK-NEXT: 9:1: End "end"
CHECK-NEXT: 9:4: Dot "."
CHECK-NEXT: [[EOFLINE:[0-9]+]]:1: Eof ""
*)
