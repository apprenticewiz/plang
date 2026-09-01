(*
'begin' is Pascal syntax.  Even given an entry, the token-description lookup
does not ask: it only consults the catalog for kinds with no fixed
spelling.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/gotbegin.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: got 'begin'
*)

//--- xx.po
msgctxt "token/Begin"
msgid "begin"
msgstr "commencer"

//--- gotbegin.pas
program p; const x = begin end.
