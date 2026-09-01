(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* issue #215: the type lowerer (CGTypes' llvmTypeOfNode/llvmTypeOfSemaType)
   used to compute an array's element count as "hi - lo + 1" in plain
   int64_t, which is signed-overflow UB once the bounds are far enough
   apart.  At the time issue #215 was fixed, Sema's own 1 GiB
   global-variable gate (issue #214) did not cover a LOCAL variable, so a
   local array declared with an extent that overflows that arithmetic
   reached this unguarded and silently lowered to a bogus (e.g.
   zero-length) array type that every index then ran off the end of,
   rather than failing to compile.

   Issue #223 (fixed separately, also on main now) extended that same 1 GiB
   gate to cover local variables too, keyed off the same byteSizeOf overflow
   detection issue #214 added -- so this exact repro is now caught earlier,
   by Sema, with a clean diagnostic, before it would ever reach the type
   lowerer this test originally targeted.  That's a strictly better outcome
   (defense in depth: two independent gates instead of one), but it means
   this specific program no longer exercises issue #215's own codegen-level
   fix directly.  This test still confirms the overflow is rejected
   cleanly rather than silently miscompiled, which remains the load-bearing
   guarantee. *)

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
CHECK: is too large to be a local variable
*)
