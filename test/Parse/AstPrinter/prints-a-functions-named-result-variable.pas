(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

(* Issue #273: ProcDecl::ResultName -- EP section 6.7.2's optional
   result-variable-specification ('= identifier' right after the parameter
   list) -- was parsed but never printed. *)

program p;
function f = r: integer;
begin r := 1 end;
begin end.

(*
CHECK: (function f () = r integer
*)
