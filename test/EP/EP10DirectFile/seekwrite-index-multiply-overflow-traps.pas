(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: SeekWrite(2305843009213693953): position is not reachable in this file
*)

(*
OUT-NOT: record 1 is now
*)

(* issue #403: SeekRead/SeekWrite/SeekUpdate compute a byte offset from n --
   (n - IndexLow) * ElemSize -- in plain unchecked int64_t arithmetic before
   handing it to fseek.  Issue #233 taught these three to check fseek's own
   return, but never checked the multiply that feeds it: a huge caller-
   supplied index overflows int64_t and wraps to a small, in-range-looking
   offset, silently landing the seek on the WRONG component instead of
   trapping -- the exact silent-corruption failure mode #233 was filed to
   close, just reached via overflow instead of via a raw negative offset.
   Concretely: a file[1..100] of integer, n = 2305843009213693953 (2^61).
   (n - 1) is 2^61, and 2^61 * 8 (ElemSize) is exactly 2^64, which wraps to
   0 in int64_t -- indistinguishable from a legitimate seek to record 1 --
   so the write that followed used to silently corrupt record 1 instead of
   the wildly out-of-range index ever being trapped. *)
program p;
var f: file [1..100] of integer;
    n: integer;
begin
  rewrite(f, 'plang_issue403_seekwrite.dat');
  write(f, 111); write(f, 222);
  n := 2305843009213693953;
  seekwrite(f, n);
  write(f, 999);
  writeln('should not reach here')
end.
