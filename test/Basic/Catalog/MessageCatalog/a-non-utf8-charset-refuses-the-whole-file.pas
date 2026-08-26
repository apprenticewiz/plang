(*
plang links no iconv, so a catalog in another encoding cannot be read at
all; refusing it whole keeps the previous one rather than mixing them. xx is
the base (loaded first); xx_YY is the delta that gets merged over it, the
same chain selectLocale() walks for any region tag -- the delta's
non-UTF-8 Content-Type must not take the base's own translation down with
it. This proves the fact observable from outside (the delta's own entry
never takes effect), not the FatalReason text itself, which nothing prints.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx_YY 2> %t.err
RUN: FileCheck --check-prefix=BASE %s < %t.err
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx_YY %t.dir/undef.pas 2> %t.err2
RUN: FileCheck --check-prefix=DELTA %s < %t.err2
*)

(*
BASE: aucun fichier d'entree
DELTA: undefined identifier 'y'
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "aucun fichier d'entree"

//--- xx_YY.po
msgid ""
msgstr ""
"Content-Type: text/plain; charset=ISO-8859-1\n"

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "autre chose"

//--- undef.pas
program p; begin y := 1 end.
