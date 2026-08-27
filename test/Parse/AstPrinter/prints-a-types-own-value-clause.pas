(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

(* Issue #273: TypeNode::InitialState -- EP section 6.6's initial-state
   specifier written on a type-denoter itself (distinct from
   VarGroup::InitExpr's own copy of the clause on a variable declaration) --
   was parsed but never printed, so `type t = integer value 0` and
   `type t = integer` dumped identically. *)

program p;
type t = integer value 0;
begin end.

(*
CHECK: (typedef t integer value 0)
*)
