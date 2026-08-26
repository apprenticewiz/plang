(*
A token description is substituted *into* a message; formatDiagMsg makes
one pass, so a %0 arriving inside an argument is not re-expanded.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/eof.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: got fin %0 fichier
*)

//--- xx.po
msgctxt "token/Eof"
msgid "end of file"
msgstr "fin %0 fichier"

//--- eof.pas
program p; begin
