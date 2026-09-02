(*
Issue #644: skipToNextConditionalMarker (Directives.cpp) used to raw-scan
dead source for '{$'/'(*$' without tracking string-literal or '//'
line-comment context at all, so a {$/(*$-LOOKING substring inside either
one was misread as one of Frame's own conditional markers: a string's own
{$ENDIF} ended the dead branch early (everything after it read live,
cascading into bogus tokens/parse errors), and a string's own {$IFDEF}
spuriously incremented the nested-depth counter (eventually a "no matching
{$ENDIF}" for a chain that was never actually unbalanced). Fixed by
recognizing a single-quoted string literal (with its doubled '' escape,
bounded to one line, matching a real string literal's own shape) and a
`//` line comment (bounded to the next newline) as opaque spans while
raw-skipping, mirrored from -- but independent of -- the identical
recognition live scanning already gives both (see
skipToNextConditionalMarker's own comment, Scanner.h, for why an ordinary
`{`/`(*`-opened comment deliberately does NOT get the same treatment: see
dead-conditional-branch-malformed-content-produces-no-diagnostic.pas
right next door for why that would break this suite's existing "malformed
dead content is never diagnosed" contract).

Each RUN below dumps tokens for a dead {$IFDEF NEVERDEFINED} branch whose
own body contains {$/(*$-looking text inside a string or a line comment:
correct behavior is the SAME either way -- everything between {$IFDEF} and
its real {$ENDIF} vanishes from the token stream (dead-conditional-
branch-is-never-tokenized.pas's own contract), and scanning resumes
cleanly with the live 'writeln(ok)' that follows, never a bogus token
from inside the dead branch and never an "unterminated conditional"
report from a depth counter thrown off by a string's own content.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang_ir -dump-tokens -std=turbo %t.dir/string-with-endif.pas | FileCheck %s
RUN: %plang_ir -dump-tokens -std=turbo %t.dir/line-comment-with-endif.pas | FileCheck %s
RUN: %plang_ir -dump-tokens -std=turbo %t.dir/string-with-ifdef.pas | FileCheck %s
*)

(*
CHECK: Begin "begin"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Identifier "writeln"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: LeftParen "("
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: StringLit "ok"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: RightParen ")"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: End "end"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Dot "."
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Eof
*)

//--- string-with-endif.pas
begin
  {$IFDEF NEVERDEFINED}
  s := 'dead {$ENDIF} text';
  {$ENDIF}
  writeln('ok')
end.

//--- line-comment-with-endif.pas
begin
  {$IFDEF NEVERDEFINED}
  // {$ENDIF}
  writeln('unreachable')
  {$ENDIF}
  writeln('ok')
end.

//--- string-with-ifdef.pas
begin
  {$IFDEF NEVERDEFINED}
  s := 'dead {$IFDEF Y} nested-looking text';
  {$ENDIF}
  writeln('ok')
end.
