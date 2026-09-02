(*
Issue #614: an isolated UTF-8 continuation byte (0x80, matching the pattern
10xxxxxx with no valid lead byte before it) used to be treated by
isUtf8ContinuationByte alone -- a byte-classification with no decoder behind
it -- as though it belonged to a multi-byte character that was never there.
SourceManager::getPresumedLoc's column count skipped it for free, so every
diagnostic later on the line landed one column left of the token it was
actually pointing at, and DiagnosticPrinter's snippet renderer copied the
raw 0x80 byte straight into the escaped, safe-to-print line it otherwise
builds.

utf8SequenceLength (StringUtil.h) now actually validates a multi-byte
sequence -- lead byte, full run of continuation bytes, no overlong/
surrogate encoding -- before treating any of its bytes as anything but its
own single display cell.  getPresumedLoc and DiagnosticPrinter::printSnippet
both walk validated sequences instead of individually-classified bytes, and
escapeControlChars (used for a diagnostic's MESSAGE text, not just the
source snippet) escapes a byte utf8SequenceLength cannot validate exactly
like a C0 control byte.

The source line below holds, in order: a space, the single raw byte 0x80,
a space, and '@' -- 0x80 occupies display/source column 2 on its own, and
'@' -- an otherwise-unremarkable Turbo address-of token here, used only
because it is one byte and easy to anchor a column on -- sits at column 4,
not the column 3 a pre-fix build reports.

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: :[[#]]:2: error: unexpected character: '\x80'
CHECK: :[[#]]:4: error: expected 'end', got '^'
*)

program p(output);
begin
 € @
end.
