(*
Only err_no_input_files is translated; everything else falls back, which is
what makes a partial translation useful rather than dangerous.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/undef.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: undefined identifier 'y'
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "aucun fichier d'entree"

//--- undef.pas
program p; begin y := 1 end.
