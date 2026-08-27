(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=42 b=0
*)

(* issue #284: readstr('42', a, b) asks for two values from a source that
   only has one; plang_io.cpp's scanNumber-based reader used to leave b
   holding whatever it held before the call -- here, a poisoned sentinel --
   instead of a defined result.  See the matching text-file test for why
   zero, not a trap, is the fix: it makes numeric reads consistent with the
   runtime's own char and binary reads, which already zero the destination
   at end-of-file, without contradicting conformance.md's documented ISO
   Sect 5.1 f) allowance to leave this unreported. *)
program p;
var a, b: integer;
begin
  a := -1; b := -1;
  readstr('42', a, b);
  write('a=', a);
  writeln(' b=', b)
end.
