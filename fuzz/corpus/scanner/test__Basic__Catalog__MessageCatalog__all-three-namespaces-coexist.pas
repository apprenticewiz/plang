(*
A message is not the only English on a diagnostic line: the severity label
comes first and the token description arrives inside the message, so a
catalog that carried only diag/ would leave every line part English.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/eof.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: erreur: attendu 'end', obtenu fin de fichier
*)

//--- xx.po
msgctxt "diag/err_expected_token"
msgid "expected %0, got %1"
msgstr "attendu %0, obtenu %1"

msgctxt "token/Eof"
msgid "end of file"
msgstr "fin de fichier"

msgctxt "label/error"
msgid "error"
msgstr "erreur"

//--- eof.pas
program p; begin
