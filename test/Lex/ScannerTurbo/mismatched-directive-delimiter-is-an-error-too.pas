(*
A directive is still a comment syntactically (skipDirective in
lib/Lex/Directives.cpp), so it closes exactly the way an ordinary Turbo
comment does -- see mismatched-comment-delimiter-is-an-error-under-turbo.pas
right next door: a comment opened with '{' must be closed with '}', and one
opened with '(*' must be closed with '*)', mixing them is an error under
-std=turbo. This is the same check with a '$' right after the opener, so
the directive path (not the plain-comment path) is what is actually under
test. Two independent sources via split-file, same reason the sibling test
uses it: the mismatched pair under test can only appear in a split-file
chunk, never in this preamble, since mixing them here would swallow the
rest of this file as one huge comment.

RUN: split-file %s %t.dir
RUN: not %plang_ir -dump-tokens -std=turbo %t.dir/brace-then-parenstar.pas 2> %t.dir/brace.err
RUN: FileCheck --check-prefix=BRACE %s < %t.dir/brace.err
RUN: not %plang_ir -dump-tokens -std=turbo %t.dir/parenstar-then-brace.pas 2> %t.dir/paren.err
RUN: FileCheck --check-prefix=PAREN %s < %t.dir/paren.err
*)

(*
BRACE: error: comment opened with '{' must be closed with '}' in Turbo Pascal mode
PAREN: error: comment opened with '(*' must be closed with '*)' in Turbo Pascal mode
*)

//--- brace-then-parenstar.pas
{$MESSAGE opened with a brace and wrongly closed *)

//--- parenstar-then-brace.pas
(*$MESSAGE opened with the alternative opener and wrongly closed }
