(*
This only proves the empty msgstr didn't produce an empty diagnostic line,
not that it was specifically counted as CatalogReport::Untranslated rather
than some other rejection reason.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: no input files
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr ""
