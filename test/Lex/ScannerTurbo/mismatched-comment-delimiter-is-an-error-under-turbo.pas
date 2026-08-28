(*
Borland's rule is the opposite of ISO 6.1.8 note 1's (exercised by
either-terminator-closes-either-comment.pas, over in ScannerLexicalAlternatives):
under -std=turbo, a comment opened with '{' must be closed with '}', and one
opened with the alternative opener must be closed with the alternative
closer -- mixing them is an error, not an accepted alternative.  Two
independent sources via split-file, same reason the ISO test right next
door uses it: the mismatched open/close pair under test can only appear in
a split-file chunk, never here in the preamble, since mixing them here
would swallow the rest of THIS file as one huge comment.  The CHECK block
below is safe to spell out literally even though it repeats those same
characters -- split-file means %s itself is never handed to plang, only
the two chunks below it are, so nothing outside them is ever scanned as
Pascal.

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
{ opened with a brace and wrongly closed *)

//--- parenstar-then-brace.pas
(* opened with the alternative opener and wrongly closed }
