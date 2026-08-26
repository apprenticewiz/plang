(*
RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: sans retour
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "sans retour"
