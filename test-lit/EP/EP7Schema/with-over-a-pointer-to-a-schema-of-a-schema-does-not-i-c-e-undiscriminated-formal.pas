(*
schemaPathOf's ROOT case (a bare `q^` or `q`) handed back the body
declaration ONE hop down and never called descendIntoInstantiation --
unlike its own FieldExpr and IndexExpr arms, which both do.  For
`outer(n) = inner(n)`, that one hop is a SchemaTypeNode, not the record
`with` needs, so `with q^ do` could not cast it and ICE'd ("'with' on a
non-record operand"), for a pointer to a schema-of-a-schema at ANY
nesting depth and regardless of whether it was reached directly or
through an undiscriminated `var` formal.
*)

(* Three levels deep, and through an undiscriminated var formal rather
   than a pointer -- the other route schemaRefOf answers for. *)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9 22
*)

program p(output);
type inner(m: integer) = record vals: array[1..m] of integer; tag: integer end;
     mid(n: integer) = inner(n);
     outer(n: integer) = mid(n);
     pouter = ^outer;
procedure showIt(var x: outer);
begin with x do begin tag := 9; vals[2] := 22 end end;
var q: pouter;
begin new(q, 5); showIt(q^);
  writeln(q^.tag:1, ' ', q^.vals[2]:1) end.
