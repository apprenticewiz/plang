(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

(*
ISO Sec6.2.2.9 lets a pointer's domain type be declared later in the same
type-definition-part.  That exception is not limited to a pointer written
directly as its own named type (`type p = ^u; u = integer;`) -- it also
applies when the pointer is buried inside another type, such as an array's
element type here.  Sema's forward-pointer patch-up pass used to walk into
Record fields only, so a stub pointer sitting in an array's ElemType was
never re-pointed at 'u' once 'u' was declared, and codegen crashed reaching
for the LLVM type of the still-unresolved stub.  (Companion case:
-forward-pointer-as-file-element.pas.  Control case, the array holding a
DIRECT forward reference rather than one through a pointer, which ISO
still prohibits: a-forward-type-reference-not-through-a-pointer-is-rejected
-array-element.pas.)
*)

program p;
type a = array[1..3] of ^u;
     u = integer;
var x: a;
begin
  new(x[1]);
  x[1]^ := 42;
  writeln(x[1]^);
  dispose(x[1])
end.
