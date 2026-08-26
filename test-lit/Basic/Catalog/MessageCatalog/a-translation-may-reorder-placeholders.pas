(*
The whole reason the format uses %0..%9 rather than positional-by-order.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx %t.dir/eof.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: end of file recu, 'end' attendu
*)

//--- xx.po
msgctxt "diag/err_expected_token"
msgid "expected %0, got %1"
msgstr "%1 recu, %0 attendu"

//--- eof.pas
program p; begin
