(*
-fcolor-diagnostics is Consumer::Both in Options.def, so the driver acts on
it and hands it on; this diagnostic comes from the other process. Paired
with the-flag-reaches-the-front-end-too-plain.pas.

RUN: split-file %s %t.dir
RUN: %plang_ir -fcolor-diagnostics %t.dir/c.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK: [1;31merror[0m
*)

//--- c.pas
program p; var x: integer;
begin x := true end.
