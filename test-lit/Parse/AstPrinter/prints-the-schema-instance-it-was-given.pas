(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

program p;
type poly(n: integer) = record c: array[0..n] of real end;
var q: poly(2);
begin end.

(*
CHECK: (schema poly 2)
*)
