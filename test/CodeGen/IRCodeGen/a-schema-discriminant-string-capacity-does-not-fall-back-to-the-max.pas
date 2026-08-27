(*
RUN: %plang_ir -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(* declaredStrCapacity's direct-StringTypeNode branch (CodeGenProcs.cpp) used
   to fall back to a fabricated PlangMaxStringCapacity (255) whenever
   sn->Capacity failed to fold through evalConstInt's own `consts` table.
   s's capacity below is the schema discriminant n, and Sema deliberately
   leaves NO ConstVal on a capacity expression that read a discriminant
   (Sema::constBound) -- exactly the "ConstVal missing" precondition this
   guards.  v's declared capacity is 5, so a correct build stores the value
   clause with capacity 5; the old fallback stored it with capacity 255,
   the wrong number for every later length/bounds computation over v.s. *)

program p(output);
type t(n: integer) = record s: string(n) value 'ab' end;
var v: t(5);
begin
  writeln('done')
end.

(*
CHECK: call void @plang_str_from_bytes(ptr @pasg_v, i64 5, ptr
CHECK-NOT: i64 255
*)
