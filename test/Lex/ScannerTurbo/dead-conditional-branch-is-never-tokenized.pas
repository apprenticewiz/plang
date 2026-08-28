(*
skipToNextConditionalMarker (lib/Lex/Directives.cpp) advances Pos raw
through a dead branch's source instead of calling next()'s ordinary token
dispatch over it, so an identifier that only exists inside a dead branch
must never reach the token stream at all -- not even as a token the Parser
later discards.  -dump-tokens is the direct way to check that: the dead
branch's own identifier is checked absent, and the real tokens on either
side of it (the ones a working scanner has to keep producing normally) are
checked present, in order, so a scanner that skipped too much or too
little would show up here as well as one that tokenized the dead branch.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

program p;
begin
  {$IFDEF NEVER}
  ThisIdentifierMustNeverReachTheTokenStream;
  {$ENDIF}
  writeln('ok')
end.

(*
CHECK: Program "program"
CHECK: Identifier "p"
CHECK: Semicolon ";"
CHECK: Begin "begin"
CHECK-NOT: ThisIdentifierMustNeverReachTheTokenStream
CHECK: Identifier "writeln"
CHECK: LeftParen "("
CHECK: StringLit "ok"
CHECK: RightParen ")"
CHECK: End "end"
CHECK: Dot "."
CHECK: Eof ""
*)
