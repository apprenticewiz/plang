(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

(*
Same forward-pointer patch-up gap as -forward-pointer-as-array-element.pas
(see that file for the full explanation), but through a File's ElemType
instead of an Array's: `file of ^u` where 'u' is declared later in the same
type-definition-part.  The file's buffer variable x^ has the stub pointer
type as its static type, so a double dereference (new(x^), then x^^) is
what forces codegen to reach for the still-unpatched pointee.
*)

program p;
type f = file of ^u;
     u = integer;
var x: f;
begin
  rewrite(x);
  new(x^);
  x^^ := 42;
  writeln(x^^)
end.
