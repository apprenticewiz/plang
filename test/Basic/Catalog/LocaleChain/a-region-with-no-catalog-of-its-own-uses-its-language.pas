(*
es_ES ships no file; it must resolve to es.po rather than to English.

RUN: %plang -fdiagnostics-language=es_ES --version | FileCheck %s
*)

(*
CHECK: Messages: es (
*)
