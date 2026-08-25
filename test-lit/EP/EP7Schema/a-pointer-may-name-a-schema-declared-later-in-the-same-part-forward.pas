(*
ISO section 6.2.2.9 lets a pointer's domain type be declared later in the
same type-definition-part, and a schema is a type like any other.  Sema
filled in each schema's parameters and body node in declaration ORDER,
so `^t` reached t while its body node was still null, took a silent
error return, and the pointer carried an error pointee all the way to
codegen -- which died with "array bounds did not fold" and no
diagnostic before it.

Swapping the two type definitions round made the identical program
compile and run, which is the clearest statement of the defect.  Both
orders must produce the identical output.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 3 
*)

program p(output);
type pl = ^t;
     t(n: integer) = array[1..n] of integer;
var a: pl; i: integer;
begin new(a, 3); for i := 1 to 3 do a^[i] := i;
  for i := 1 to 3 do write(a^[i]:1, ' '); writeln end.
