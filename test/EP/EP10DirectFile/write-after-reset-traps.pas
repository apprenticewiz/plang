(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: write: file is not open in the required mode
*)

(*
OUT-NOT: should not reach here
*)

(* issue #124: reset() opens the external file read-only (C fopen mode "r"),
   so a write to f after reset must be a dynamic-violation (ISO Sect 6.7.5.6)
   rather than a silently discarded write that leaves the file untouched. *)
program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f, 'plang_issue124_write_after_reset.dat');
  v := 42;
  write(f, v);
  reset(f, 'plang_issue124_write_after_reset.dat');
  v := 99;
  write(f, v);
  writeln('should not reach here')
end.
