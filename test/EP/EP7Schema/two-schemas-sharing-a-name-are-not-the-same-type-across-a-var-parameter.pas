(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: declares two distinct types
*)

(* Issue #255: schemaInstMatch (the var-parameter arm of "are these two
   SchemaInstance types the same") compared only SchemaName and discriminant
   VALUES, so two `vec(3)` from unrelated declarations that happen to share a
   spelling were accepted as the same type -- the same defect
   isAssignCompatible was given a declaration-identity fix for (c03cd04), in
   the one place that fix did not reach.  b's `vec` is an array of real; r's
   parameter names an unrelated, same-named array of integer -- passing one
   for the other must be rejected, not aliased. *)

program p(output);
type vec(n: integer) = array[1..n] of integer;
procedure r(var x: vec(3));
begin x[1] := 99 end;
procedure q;
type vec(n: integer) = array[1..n] of real;
var b: vec(3);
begin b[1] := 1.5; r(b); writeln(b[1]:1:1) end;
begin q end.
