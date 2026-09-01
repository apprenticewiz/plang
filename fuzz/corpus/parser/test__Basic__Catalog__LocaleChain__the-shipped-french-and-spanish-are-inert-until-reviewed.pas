(*
fr, es, fr_CA and es_MX ship entirely fuzzy for err_no_input_files on
purpose. Without -fdiagnostics-show-fuzzy they must load and change
nothing -- the diagnostic stays English even though a catalog was found.

RUN: not %plang -fdiagnostics-language=fr 2> %t.fr.err
RUN: FileCheck %s < %t.fr.err
RUN: not %plang -fdiagnostics-language=es 2> %t.es.err
RUN: FileCheck %s < %t.es.err
RUN: not %plang -fdiagnostics-language=fr_CA 2> %t.frca.err
RUN: FileCheck %s < %t.frca.err
RUN: not %plang -fdiagnostics-language=es_MX 2> %t.esmx.err
RUN: FileCheck %s < %t.esmx.err
*)

(*
CHECK: no input files
*)
