(*
One typo in a translation should lose one message, not the rest of the
catalog. The first entry's msgstr is missing its closing quote, spilling
into what would otherwise be the next entry -- the reader must still recover
and load the second, independent entry.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx 2> %t.err
RUN: FileCheck --check-prefix=FIRST %s < %t.err
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/undef.pas 2> %t.err2
RUN: FileCheck --check-prefix=SECOND %s < %t.err2
*)

(*
FIRST: no input files
SECOND: identifiant non defini 'y'
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "bon

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "identifiant non defini '%0'"

//--- undef.pas
program p; begin y := 1 end.
