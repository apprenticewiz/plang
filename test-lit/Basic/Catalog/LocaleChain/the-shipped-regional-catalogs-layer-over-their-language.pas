(*
Against the real shipped catalogs, since the bug this covers was only
visible with a delta small enough to leave most messages to the base.
es_MX.po overrides only a few dozen entries against es.po's much larger
list; err_undefined_identifier is one es.po carries (as a fuzzy entry) that
es_MX.po does not itself mention at all -- -fdiagnostics-show-fuzzy is what
the original in-process test asked for too (HonourFuzzy=true), and it's
what makes es.po's base layer visible here rather than staying English.

RUN: %plang -fdiagnostics-language=es_MX --version | FileCheck --check-prefix=FOUND %s
RUN: split-file %s %t.dir
RUN: not %plang -fdiagnostics-language=es_MX -fdiagnostics-show-fuzzy %t.dir/undef.pas 2> %t.err
RUN: FileCheck --check-prefix=BASE %s < %t.err
*)

(*
FOUND: Messages: es_MX (
BASE: identificador no definido «y»
*)

//--- undef.pas
program p; begin y := 1 end.
