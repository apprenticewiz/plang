(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck %s
*)

(* Issue #273: ModuleNode::IsImplementation -- EP section 6.11.1's
   'implementation' module-identification -- was parsed but never printed;
   only IsInterface's "interface" marker was, so an implementation module and
   a module with neither marker dumped identically. *)

module m interface;
end.
module m implementation;
end.

(*
CHECK: (module m interface
CHECK: (module m implementation
*)
