(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: read: input does not start with a valid number
*)

(*
OUT-NOT: should not reach here
*)

(* issue #236: readstr shares plang_io.cpp's scanNumber with `read`/`readln`
   on standard input (EP Sect 6.7.5.5 defines readstr in terms of an
   auxiliary file read); a token that cannot start a number used to leave
   the destination variable silently unchanged instead of reporting
   anything. *)
program p;
var i: integer;
begin
  i := 42;
  readstr('xyz', i);
  writeln('should not reach here: ', i)
end.
