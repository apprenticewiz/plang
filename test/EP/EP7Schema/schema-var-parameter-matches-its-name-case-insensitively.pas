(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

(* Issue #268: schemaInstMatch, the var-parameter arm of SchemaInstance
   identity, compared SchemaName with a bare `!=` on the as-written spelling,
   inconsistent with every other identifier comparison in Sema (toLower/eqCI
   -- see sameParamType's undiscriminated-Schema arm and isAssignCompatible's
   Schema arm just below it).  A schema declared as VEC and a var parameter
   spelled vec(3) name the very same declaration; the case difference alone
   must not be a type mismatch. *)

program p(output);
type VEC(n: integer) = array[1..n] of integer;
procedure q(var x: vec(3));
begin x[1] := 42 end;
var b: VEC(3);
begin q(b); writeln(b[1]:0) end.
