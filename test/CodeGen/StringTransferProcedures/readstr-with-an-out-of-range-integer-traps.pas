(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: read: '9223372036854775808' is out of range for an integer
*)

(*
OUT-NOT: should not reach here
*)

(* issue #240: plang_io.cpp's strtoll-based reader (readstr/read on stdin)
   computed the same silently-clamped value the fscanf-based file reader did
   but never checked ERANGE to notice -- ISO Sect 6.9.1 requires the value
   read be assignment-compatible with the variable's type, so this must trap
   rather than silently produce maxint. *)
program p;
var i: integer;
begin
  i := 0;
  readstr('9223372036854775808', i);
  writeln('should not reach here: ', i)
end.
