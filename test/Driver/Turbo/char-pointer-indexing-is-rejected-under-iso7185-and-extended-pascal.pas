(*
The indexing sibling of char-pointer-arithmetic-is-rejected-under-iso7185-
and-extended-pascal.pas: `p[i]` on a Char-pointee pointer is legal Turbo
(Sema::checkIndex's Pointer arm, SemaExpr.cpp, gated on Opts.turbo() the
same way), but neither ISO 7185 nor Extended Pascal gives a pointer any
subscript operator at all -- indexing one falls through checkIndex's
ordinary "not an array" case, since with Opts.turbo() false the Pointer arm
above it never fires.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: subscript operator applied to non-array type '^char'
*)

program p;
type
  mycharptr = ^char;
var
  ptr: mycharptr;
  c: char;
begin
  new(ptr);
  c := ptr[0];
end.
