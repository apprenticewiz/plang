(*
RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/eof.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: got fin de fichier
*)

//--- xx.po
msgctxt "token/Eof"
msgid "end of file"
msgstr "fin de fichier"

//--- eof.pas
program p; begin
