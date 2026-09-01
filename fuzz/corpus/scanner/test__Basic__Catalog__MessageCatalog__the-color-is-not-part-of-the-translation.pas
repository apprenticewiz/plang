(*
An SGR escape is not language, and a catalog must not be able to inject one
-- which is also why the reader refuses \x1b in a catalog entry in the
first place (see an-escape-sequence-outside-the-whitelist-is-refused.pas).
-fcolor-diagnostics forces color regardless of whether stderr is a TTY.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx -fcolor-diagnostics 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: [1;31merreur[0m: no input files
*)

//--- xx.po
msgctxt "label/error"
msgid "error"
msgstr "erreur"
