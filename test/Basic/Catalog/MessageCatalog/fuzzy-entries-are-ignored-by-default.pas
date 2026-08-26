(*
This only proves the fuzzy entry was ignored, not that it was specifically
counted as CatalogReport::Fuzzy rather than some other rejection reason; see
test-lit/Basic/Catalog/MessageCatalog/fuzzy-entries-can-be-honoured-for-review.pas
for -fdiagnostics-show-fuzzy proving the entry really is there, just skipped.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: no input files
*)

//--- xx.po
#, fuzzy
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "une supposition"
