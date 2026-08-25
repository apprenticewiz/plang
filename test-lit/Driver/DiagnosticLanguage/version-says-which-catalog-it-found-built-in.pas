(*
The only positive evidence a catalog was found. Everything else about a
missing one looks exactly like not having asked for one. Paired with
version-says-which-catalog-it-found-qps-ploc.pas.

RUN: %plang_ir --version > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK: Messages: en_US (built-in)
*)
