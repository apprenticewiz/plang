(*
formatDiagMsg would substitute nothing and print a sentence with a hole,
reporting nothing wrong.  Better to keep the English.  This only proves the
translation didn't apply -- it can't tell "dropped a placeholder" apart from
any other rejection reason a catalog entry can fail for; see
test/Basic/catalog_test.cpp's MessageCatalog suite for the exact-bucket
(CatalogReport::Malformed) version of this same case.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/eof.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: expected 'end', got end of file
*)

//--- xx.po
msgctxt "diag/err_expected_token"
msgid "expected %0, got %1"
msgstr "attendu %0"

//--- eof.pas
program p; begin
