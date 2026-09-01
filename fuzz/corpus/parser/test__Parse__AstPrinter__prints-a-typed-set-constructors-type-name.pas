(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

(* Issue #273: SetLiteralExpr::TypeName -- EP section 6.8.7.4's optional
   type-name prefix on a set constructor, which is exactly what tells
   Sema's checkSetLit this is a TYPED constructor rather than the untyped []
   literal -- was parsed but never printed, making the two indistinguishable
   in the dump. *)

program p;
type cs = set of char;
var s: cs;
begin s := cs['a'..'c'] end.

(*
CHECK: (assign s cs[(.. "a" "c")])
*)
