(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* ISO §6.5.3.2 / EP §6.7.3.7: an index expression into a conformant array
   must be assignment-compatible with the dimension's declared ordinal type,
   the same check a plain array's index already gets (see checkIndex's
   Array branch).  Indexing a char-indexed conformant array with an integer
   must be rejected at compile time, not accepted and left to a runtime
   range check (or silent mis-indexing). *)

(*
ERR: index type mismatch
*)

program p;
procedure setElem(var a: array [lo..hi: char] of integer);
begin
  a[97] := 1
end;
var arr: array ['a'..'e'] of integer;
begin
  setElem(arr)
end.
