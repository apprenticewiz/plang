(*
The same unbounded-ordinal treatment as bytebool-is-not-a-set-base-type.pas
(this directory), but through the array-index-type gate instead of the set-
base one: ordinalRange(WordBool) is nullopt (Type::IsLooseBool), so
`array[WordBool]` hits the same "not a bounded ordinal" refusal a bare
Integer index would (SemaType.cpp's ArrayTypeNode arm), rather than being
given a 65536-element extent.  Checked against real fpc 3.2.2, which refuses
this too ("Data element too large").

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'WordBool' cannot be an array index type; an index type is an ordinal type with a bounded range
*)

var
  a: array[WordBool] of Integer;
begin
end.
