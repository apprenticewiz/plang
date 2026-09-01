(*
Untranslated severities keep the English, one message at a time -- the
error label below is translated, the warning label a second RUN line
triggers is not.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/error.pas 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: env PLANG_LOCALE_DIR=%t.dir %plang -fdiagnostics-language=xx %t.dir/warn.pas 2> %t.err2
RUN: FileCheck --check-prefix=WARN %s < %t.err2
*)

(*
ERR: erreur: undefined identifier
WARN: warning: label '10'
*)

//--- xx.po
msgctxt "label/error"
msgid "error"
msgstr "erreur"

//--- error.pas
program p; begin y := 1 end.

//--- warn.pas
program p; label 10; var x : integer; begin 10: x := 0 end.
