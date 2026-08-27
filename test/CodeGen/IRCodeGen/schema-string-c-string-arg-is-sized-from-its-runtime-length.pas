(*
RUN: %plang_ir -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(* issue #216: reset/rewrite/extend/update's optional filename argument is
   marshalled into a NUL-terminated C string by emitCStrArg
   (StringCallMarshalling.cpp), since the runtime entry points take a
   `const char *` and a string(n) value has no terminator of its own. q^
   here is the undiscriminated `string` schema's probe type -- new(q, 300)
   fixes its real capacity only at run time, so ExprStrCap (the static
   probe used to size a REUSABLE buffer) can only ever widen to
   PlangMaxStringCapacity (255) for it, the same way it does for any other
   discriminant-fixed capacity. The unfixed codegen allocated exactly that
   -- `alloca [256 x i8]` -- and then memcpy'd the string's ACTUAL runtime
   length into it, which new(q, 300) legitimately made 300: a 45-byte stack
   buffer overflow (confirmed under AddressSanitizer, which reports a
   stack-buffer-overflow WRITE of size 300 into str.cstr's 256-byte frame
   slot). Fixed by sizing the buffer itself, at run time, from the very
   same length value that fills it -- CreateDynAlloca, the header-less
   sibling of the createDynStrAlloca a discriminant-sized string
   concatenation already used for the same reason (CGBinaryOps.cpp). *)

program p;
var f: file of integer;
    q: ^string;
begin
  new(q, 300);
  reset(f, q^)
end.

(*
CHECK-NOT: alloca [256 x i8]
CHECK: %str.cstr.size = add i64 %str.len, 1
CHECK-NEXT: %str.cstr = alloca i8, i64 %str.cstr.size
CHECK: call void @llvm.memcpy.p0.p0.i64(ptr %str.cstr, ptr %str.data, i64 %str.len,
*)
