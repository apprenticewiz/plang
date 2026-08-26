(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p;
procedure q(a: array[lo..hi: integer] of integer); begin end;
begin end.

(*
CHECK: (conformant-array (lo hi integer) integer)
*)
