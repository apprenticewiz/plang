(*
Token-level proof that {$I file} really does switch the Scanner onto a new
buffer rather than, say, textually pasting the included file's characters
into main.pas's own Text: alpha (main.pas line 1), beta (inc.pas -- its OWN
line 1, not main.pas's line 3, since SourceManager gives every buffer its
own coordinate space -- see SourceManager.h's header comment), and gamma
(back in main.pas, its own line 3, resumed exactly where the directive
ended) are three tokens from two different FileIDs with two independently
numbered line counts, spliced into one seamless token stream with exactly
one Eof at the very end -- never one where the included buffer runs out,
which next()'s own popInclude check (Scanner.cpp) exists specifically to
avoid.  -dump-tokens prints only line:col, no filename, so this cannot by
itself prove WHICH file each token came from (test/Driver/Turbo's own
a-syntax-error-inside-an-included-file-reports-its-own-location.pas proves
that, through a real diagnostic's path instead); what it proves is that the
included buffer's own line numbering is independent of main.pas's, which
only holds if a distinct buffer is genuinely being read.

RUN: split-file %s %t.dir
RUN: %plang_ir -dump-tokens -std=turbo %t.dir/main.pas | FileCheck %s
*)

(*
CHECK: 1:1: Identifier "alpha"
CHECK-NEXT: 1:1: Identifier "beta"
CHECK-NEXT: 3:1: Identifier "gamma"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Eof
*)

//--- inc.pas
beta

//--- main.pas
alpha
{$I inc.pas}
gamma
