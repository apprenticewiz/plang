(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: read: file is not open in the required mode
*)

(*
OUT-NOT: should not reach here
*)

(* issue #124: rewrite() opens the external file write-only (C fopen mode
   "w"), and seekread only repositions and marks f readable without
   reopening the stream -- so a read after seekread on a file still open
   from rewrite must be a dynamic-violation (ISO Sect 6.7.5.6) rather than
   silently leaving the read variable at its prior, unrelated value. *)
program p;
var f: file of integer;
    v: integer;
begin
  v := 7;
  rewrite(f, 'plang_issue124_read_while_rewrite.dat');
  write(f, 123);
  seekread(f, 0);
  read(f, v);
  writeln('should not reach here: ', v)
end.
