(*
A catalog shipped entirely fuzzy is inert, so whoever reviews it needs a way
to see the thing they are reviewing.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx -fdiagnostics-show-fuzzy 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: une supposition
*)

//--- xx.po
#, fuzzy
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "une supposition"
