(*
Issue #600's other case-label context: a variant-part's own 'of'
(ParseType.cpp's parseVariantPart) has the identical ambiguity a
case-STATEMENT's 'of' does (case-stmt-caret-control-labels.pas,
AstPrinter) -- it always introduces case-constant labels, never a
type-denoter, unlike `array of`/`set of`/`file of`'s own 'of' elsewhere
in this file, which must NOT get this treatment. See
record-variant-part.pas right next door for the plain-integer-label
baseline this extends. The two control bytes below (1 and 13) are the
real ordinal values `^A`/`^M` denote -- not their two-character spelling
-- so this proves the parser read them as control-character constants,
not a Caret token starting a (senseless, in label position) deref. Loose
(not --match-full-lines) CHECK matching: the embedded carriage-return byte
(13) in the second label's own printed value would otherwise read as its
own line break to a full-line matcher.
*)

(*
RUN: %plang_ir -dump-parse-tree -std=turbo %s | FileCheck %s
*)

program p;
type Shape = record
  case kind : char of
    ^A: (radius : real);
    ^M: (width, height : real)
end;
begin end.

(*
CHECK: (typedef Shape (record (case kind : char ("" : (radius real)) ("" : (width height real)))))
*)
