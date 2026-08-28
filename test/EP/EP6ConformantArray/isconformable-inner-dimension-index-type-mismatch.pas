(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* Issue #406: #263's diagnostic-selection logic in checkCallArgs only
   inspected the OUTERMOST dimension of a conformant-array parameter, so a
   mismatch buried in an INNER dimension still fell through to the generic,
   misleading "element type mismatch" message #263 was written to stop
   producing -- even though isConformable (which DOES recurse through every
   dimension) was the very thing that correctly rejected the call.
   Dimension 1 here (integer-indexed) matches on both sides; dimension 2's
   index type does not -- char in the schema, an integer subrange in the
   actual -- the same class of mismatch
   isconformable-checks-index-type-and-packedness.pas already covers for a
   single, OUTER dimension.  This is its two-dimension sibling: the precise
   "index type mismatch" diagnostic must name dimension 2's types, not
   dimension 1's or a generic "element type mismatch". *)

(*
ERR: index type mismatch for conformant array parameter 'a': schema expects 'char', argument has '1..3'
*)

program p;
procedure proc(a: array [r1..r2: integer; c1..c2: char] of integer);
begin end;
type Row = array [1..3] of integer;
var m: array [1..3] of Row;
begin
  proc(m)
end.
