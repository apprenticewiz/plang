(*
R4.  The record arm of llvmTypeOfSemaType had the resolved type T in
hand and passed only T.RecordDecl to the layout, which then re-read each
field's DENOTER.  One declaration node serves every instantiation and
carries whichever was resolved last -- so a program that mentions the
schema undiscriminated ANYWHERE laid out every discriminated instance of
it with the probe's field sizes:

  without `procedure body(var v: t)`   ( [4 x i64], [4 x i64], i64 )
  with it                              ( [4 x i64], [1 x i64], i64 )

Merely DECLARING the parameter changed the layout of an unrelated
variable.  The Sema-against-codegen offset check was green through it,
because both sides were reading the same stale annotation: two answers
agreeing is not the same as either being right.

The two programs must agree, which is the whole assertion -- the second
one only adds a procedure that the first does not have.
*)

(* Only adds a procedure mentioning the schema undiscriminated; must
   produce the identical output to the plain case. *)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:400 99
*)

program p(output);
type inner(m: integer) = array[1..m] of integer;
     t(n: integer) = record a: array[1..n] of integer;
                             x: inner(n); k: integer end;
var a: t(4); i: integer;
procedure body(var v: t);
begin writeln(v.x[4]:1, ' ', v.k:1) end;
begin for i := 1 to 4 do begin a.a[i] := i; a.x[i] := i * 100 end;
  a.k := 99; body(a) end.
