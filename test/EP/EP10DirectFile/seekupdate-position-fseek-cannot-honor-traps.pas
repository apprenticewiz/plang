(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: SeekUpdate(0): position is not reachable in this file
*)

(*
OUT-NOT: should not reach here
*)

(* issue #233: SeekRead/SeekWrite/SeekUpdate computed a byte offset from n --
   (n - IndexLow) * ElemSize -- and handed it to fseek without checking
   fseek's return.  A seek behind the index type's origin computes a
   negative offset, which fseek rejects (EINVAL); the unchecked return let
   that failure pass silently, leaving the stream positioned wherever the
   prior operation had left it, so the write that followed landed on that
   unrelated component instead of trapping.
   Concretely: a file[1..100] of char holding "abc", updated with
   seekupdate(2)+write('X') (which lands correctly, on record 2, and leaves
   the stream positioned at record 3) then seekupdate(0) -- invalid, since 0
   is below the declared index origin of 1 -- +write('Z'), used to come out
   "aXZ": the second write silently landed on record 3 ('c') rather than the
   requested (and unreachable) record 0 ever being trapped. *)
program p;
var f: file [1..100] of char;
begin
  rewrite(f, 'plang_issue233_seekupdate.dat');
  write(f, 'a'); write(f, 'b'); write(f, 'c');
  update(f, 'plang_issue233_seekupdate.dat');
  seekupdate(f, 2);
  write(f, 'X');
  seekupdate(f, 0);
  write(f, 'Z');
  writeln('should not reach here')
end.
