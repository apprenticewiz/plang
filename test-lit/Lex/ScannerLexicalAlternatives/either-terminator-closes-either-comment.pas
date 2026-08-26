(*
ISO section 6.1.8 note 1: a comment may open with a brace and close with
the alternative closer, or open with the alternative opener and close
with a brace.  Both mixtures were once rejected as a mismatch.  Two
independent sources (not one file with two RUN lines against the same
source), via split-file, since each exercises its own comment-opener/
closer mixture; the literal open/close characters below can only appear
in a split-file chunk, never in this preamble or the trailing CHECK block
-- plang's own comment syntax closes on EITHER terminator, so a literal
close character here would end this comment early.

RUN: split-file %s %t.dir
RUN: %plang_ir -dump-tokens %t.dir/brace-opened.pas | FileCheck --check-prefix=BRACE %s
RUN: %plang_ir -dump-tokens %t.dir/paren-opened.pas | FileCheck --check-prefix=PAREN %s
*)

(*
BRACE: IntLit "42"
PAREN: IntLit "42"
*)

//--- brace-opened.pas
{ opened with a brace *) 42

//--- paren-opened.pas
(* opened with a parenthesis } 42
