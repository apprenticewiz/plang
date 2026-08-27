(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* issue #215: the type lowerer (CGTypes' llvmTypeOfNode/llvmTypeOfSemaType)
   used to compute an array's element count as "hi - lo + 1" in plain
   int64_t, which is signed-overflow UB once the bounds are far enough
   apart.  Sema's own 1 GiB global-variable gate (issue #214) does not cover
   a LOCAL variable, so a local array declared with an extent that overflows
   that arithmetic used to reach this unguarded and silently lower to a
   bogus (e.g. zero-length) array type that every index then ran off the end
   of, rather than failing to compile.  This confirms it is now a clean
   compile-time failure instead. *)

program p;

procedure q;
var a: array[-maxint-1..maxint] of char;
begin
  a[0] := 'x'
end;

begin
  q
end.

(*
CHECK: plang codegen:
*)
