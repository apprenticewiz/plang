(*
RUN: %plang %s -o %t
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

(* issue #240: 9223372036854775808 is one past maxint (2^63 - 1).
   fscanf's "%lld" conversion silently clamped an overflowing text token
   instead of reporting it (an overflowing scanf numeric conversion is
   undefined behaviour in C; glibc's happens to clamp) -- ISO Sect 6.9.1
   requires the value read be assignment-compatible with the variable's
   type, so this must trap rather than silently produce maxint. *)
program p;
var f: text; i: integer;
begin
  i := 0;
  rewrite(f);
  writeln(f, '9223372036854775808');
  reset(f);
  read(f, i);
  writeln('should not reach here: ', i)
end.
