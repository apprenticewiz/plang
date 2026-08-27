(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

(* Issue #273: VarGroup::InitExpr -- EP section 6.4.1's optional 'value'
   initializer on a variable declaration -- was parsed but never printed, so
   every value clause vanished from the dump. *)

program p;
var x: integer value 5;
begin end.

(*
CHECK: (var (x) integer value 5)
*)
