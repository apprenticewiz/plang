(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: a string of length 2 cannot fill a 5-character string-type
*)

(* issue #232: ISO §6.4.3.2 requires a string-type assignment's source to have
   exactly the destination's length, but the codegen guard for it only ran
   when the source's CAPACITY was itself unknown to Sema (a discriminant-fixed
   string).  s's capacity of 5 matches a's length statically, so Sema let the
   assignment through -- but s's run-time LENGTH is a field independent of its
   capacity, and after `s := 'hi'` it is 2, not 5.  Copying all 5 bytes
   regardless left a with 3 stale bytes from the earlier, longer assignment
   ("hillo" instead of a checked failure). *)
program p;
var s: string(5); a: packed array[1..5] of char;
begin
  s := 'hello';
  s := 'hi';
  a := s;
  writeln('should not get here')
end.
