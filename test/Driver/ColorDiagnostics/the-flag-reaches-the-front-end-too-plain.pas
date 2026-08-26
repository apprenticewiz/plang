(*
-fcolor-diagnostics is Consumer::Both in Options.def, so the driver acts on
it and hands it on; this diagnostic comes from the other process. Without
the flag, the front end's own diagnostic stays plain, same as the driver's.
Paired with the-flag-reaches-the-front-end-too-color.pas.

RUN: split-file %s %t.dir
RUN: %plang_ir %t.dir/c.pas > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: [
*)

//--- c.pas
program p; var x: integer;
begin x := true end.
