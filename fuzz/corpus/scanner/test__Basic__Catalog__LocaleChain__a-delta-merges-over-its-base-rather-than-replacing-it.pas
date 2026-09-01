(*
A regional catalog names only what differs, so the loader has to lay one
over the other: loading only the most specific file leaves every message it
does not name in English, which is the opposite of what a delta catalog is
for. xx is the base (two messages), xx_YY the delta (one of them,
overridden) -- the one the delta is silent about must still come from the
base, not from English.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx_YY 2> %t.err
RUN: FileCheck --check-prefix=OVERRIDDEN %s < %t.err
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx_YY %t.dir/undef.pas 2> %t.err2
RUN: FileCheck --check-prefix=FROMBASE %s < %t.err2
*)

(*
OVERRIDDEN: delta uno
FROMBASE: base dos 'y'
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "base uno"

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "base dos '%0'"

//--- xx_YY.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "delta uno"

//--- undef.pas
program p; begin y := 1 end.
