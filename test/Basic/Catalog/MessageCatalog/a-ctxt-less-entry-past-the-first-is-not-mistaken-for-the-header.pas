(*
msgctxt is optional in gettext, and most real-world entries never carry one,
so a plain "msgid / msgstr" pair with no msgctxt is not automatically the
catalog header -- only the one true header (msgid "") is, and only the first
one. A normal message whose msgstr happens to contain a line starting
"Content-Type:" -- pure coincidence, not metadata -- must not be misread as
the header and take the rest of the catalog down with it over a "charset"
that was never actually declared.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx 2> %t.err
RUN: FileCheck --check-prefix=FIRST %s < %t.err
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/undef.pas 2> %t.err2
RUN: FileCheck --check-prefix=SECOND %s < %t.err2
*)

(*
FIRST: aucun fichier d'entree
SECOND: identifiant non defini 'y'
*)

//--- xx.po
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "aucun fichier d'entree"

msgid "some unrelated string"
msgstr "Content-Type: text/plain; charset=ISO-8859-1"

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "identifiant non defini '%0'"

//--- undef.pas
program p; begin y := 1 end.
