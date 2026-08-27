(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #207, EP-only sibling: extend/update share reset/rewrite's CodeGen
   arm (file, plus an optional external filename) and had the same stale
   Builtins.def -1 upper bound, which let a third argument through unchecked. *)

program p; var f: file of integer; begin extend(f, 1, 2) end.

(*
CHECK: 'extend' expects 1 or 2 argument(s), got 3
*)
