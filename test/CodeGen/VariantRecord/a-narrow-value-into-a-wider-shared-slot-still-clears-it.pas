(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:120
*)

(* issue #229: a subrange is always stored i64 (see llvmTypeOfSemaTypeImpl),
   whatever its host type -- so assigning a char (i8) into a subrange-of-char
   field is a widening store, not a same-width one.  x.i := -1 sets every
   byte of the shared storage; if x.c := 'x' then stores only the low byte
   (missing the i8 -> i64 zext), the high 7 bytes stay -1's, and reading the
   whole i64 slot back through ord(x.c) sees them. *)
program p(output);
type u = record
       case boolean of true: (i: integer); false: (c: 'a'..'z') end;
var x: u;
begin x.i := -1; x.c := 'x'; writeln(ord(x.c)) end.
