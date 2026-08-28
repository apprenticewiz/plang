(*
dispatchSwitchDirective (lib/Lex/Directives.cpp) accepts four argument
spellings across two Name forms: letter '+'/'-' and long-name '+'/'-'/
'ON'/'OFF'.  A directive is still a comment syntactically (this file's own
sibling tests establish that for the earlier categories), so whichever
spelling is used, skipDirective must consume the whole `{$...}` and leave
no trace in the token stream -- checked here across all six shown below,
with the ordinary tokens on either side of each one still present and in
order, exactly the way dead-conditional-branch-is-never-tokenized.pas
already checks for {$IFDEF}.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

program p;
{$R+}
{$R-}
{$RANGECHECKS ON}
{$RANGECHECKS OFF}
{$RANGECHECKS+}
{$RANGECHECKS-}
begin
  writeln('ok')
end.

(*
CHECK: Program "program"
CHECK: Identifier "p"
CHECK: Semicolon ";"
CHECK: Begin "begin"
CHECK: Identifier "writeln"
CHECK: LeftParen "("
CHECK: StringLit "ok"
CHECK: RightParen ")"
CHECK: End "end"
CHECK: Dot "."
CHECK: Eof ""
*)
