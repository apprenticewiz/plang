(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Regression test for issue #407, a regression from issue #172 (eea977f).
   #172 changed isAssignCompatible's Pointer arm to require isIdenticalType
   between the two pointees instead of recursing into isAssignCompatible --
   correct for its own target (see the sibling test
   pointer-domains-must-be-identical-not-just-assignable.pas, which this
   test must NOT weaken), since that recursion let a subrange leak its own
   compatibility with its base type through a pointer.

   But a SchemaInstance pointee is deliberately never interned by
   TypeContext -- every syntactic `Vec(4)` mints a fresh Type object, even
   for the identical schema+discriminant combination (see schemaInstMatch's
   comment in SemaExpr.cpp) -- so two independently-declared pointer-type
   aliases naming the identical schema instantiation were wrongly rejected
   as incompatible types by isIdenticalType alone, where the pre-#172
   recursive call used to recurse into the SchemaInstance branch and accept
   them correctly. The same identity gap also broke passing one alias as a
   value or var parameter typed with the other, and pointer equality
   between the two aliases. *)

program p;
type Vec(n: integer) = array[1..n] of integer;
type VecPtrA = ^Vec(4);
type VecPtrB = ^Vec(4);
var a: VecPtrA; b: VecPtrB;

procedure valueParam(x: VecPtrB);
begin
  writeln(x^[1]:1, ' ', x^[2]:1, ' ', x^[3]:1, ' ', x^[4]:1)
end;

procedure varParam(var x: VecPtrA);
begin
  x^[1] := 99
end;

begin
  new(a);
  a^[1] := 1; a^[2] := 4; a^[3] := 9; a^[4] := 16;

  (* Assignment across two independently-declared aliases of the identical
     schema instantiation -- the issue's own repro. *)
  b := a;
  writeln(b^.n:1);

  (* Passing a value of one alias where a formal is typed with the other. *)
  valueParam(b);

  (* Pointer equality/inequality across the two aliases. *)
  writeln(a = b);
  writeln(a <> b);

  (* Passing one alias as a var parameter typed with the other. *)
  varParam(b);
  writeln(a^[1]:1)
end.

(*
CHECK:4
CHECK-NEXT:1 4 9 16
CHECK-NEXT:true
CHECK-NEXT:false
CHECK-NEXT:99
*)
