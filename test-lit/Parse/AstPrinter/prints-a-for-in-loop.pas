(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p; var c: char; s: set of char;
begin for c in s do end.

(*
CHECK: (for-in c s
*)
