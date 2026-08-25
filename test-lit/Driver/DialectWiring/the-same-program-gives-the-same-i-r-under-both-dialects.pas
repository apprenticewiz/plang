(* A program using no dialect feature must lower identically under the
   default (ISO 7185) dialect and under -std=iso10206. *)

(*
RUN: %plang_ir -emit-llvm %s -o %t1.ll
RUN: %plang_ir -std=iso10206 -emit-llvm %s -o %t2.ll
RUN: diff %t1.ll %t2.ll
*)

program p(output);
var i: integer;
begin for i := 1 to 3 do writeln(i) end.
