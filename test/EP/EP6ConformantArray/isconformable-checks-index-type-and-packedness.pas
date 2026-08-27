(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* ISO §6.7.3.8: isConformable has to compare the actual's index type against
   the ordinal type named in the schema (a) and the packedness of the two
   (d), not only the element type -- a char-indexed schema does not conform
   to an integer-indexed actual, and an unpacked actual does not conform to a
   packed schema, even where the element types agree. *)

(*
ERR: index type mismatch for conformant array parameter 'a': schema expects 'char', argument has '1..10'
ERR: conformant array parameter 'a' is packed but the argument is unpacked
*)

program p;
procedure byIndexType(var a: array [l..h: char] of integer);
begin end;
procedure byPackedness(a: packed array [l..h: integer] of integer);
begin end;
var ints: array [1..10] of integer;
var flags: array [1..10] of integer;
begin
  byIndexType(ints);
  byPackedness(flags)
end.
