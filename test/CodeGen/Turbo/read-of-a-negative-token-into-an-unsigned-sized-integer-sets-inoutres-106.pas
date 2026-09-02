(*
Issue #672: reading a leading '-' token into an UNSIGNED destination
(Byte/Word/Cardinal/LongWord -- QWord already got this right) used to reach
plang_read_file_i64_turbo, whose strtoll happily parses a negative int64_t,
which CoerceToType's narrowing then wrapped into the unsigned destination
with no trap at all -- "-1" into a Word silently became 65535 with
IOResult 0.  fpc -Mtp instead sets InOutRes 106 and leaves the destination
at 0, exactly like it already did (and plang already matched) for QWord.
The fix routes every UNSIGNED sized-integer rung, not just QWord, through
the "_u64"/"_u64_turbo" reader family, whose leading-'-' check
(plang_file.cpp) already existed for QWord alone.

A value that is positive but too wide for the destination's own narrower
range (e.g. 70000 into a Word) is a separate, pre-existing, and -- confirmed
against fpc -Mtp -- CORRECT silent-wraparound case (io=0): this test's last
line guards that CoerceToType's truncation still does that unchanged.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:byte: 0 io=106
CHECK-NEXT:word: 0 io=106
CHECK-NEXT:cardinal: 0 io=106
CHECK-NEXT:longword: 0 io=106
CHECK-NEXT:qword: 0 io=106
CHECK-NEXT:word: 4464 io=0
*)

//--- test.pas
var
  b: Byte;
  w: Word;
  c: Cardinal;
  lw: LongWord;
  q: QWord;
  io: Integer;
begin
  {$I-}
  read(b);  io := IOResult; writeln('byte: ', b, ' io=', io);
  read(w);  io := IOResult; writeln('word: ', w, ' io=', io);
  read(c);  io := IOResult; writeln('cardinal: ', c, ' io=', io);
  read(lw); io := IOResult; writeln('longword: ', lw, ' io=', io);
  read(q);  io := IOResult; writeln('qword: ', q, ' io=', io);
  read(w);  io := IOResult; writeln('word: ', w, ' io=', io);
  {$I+}
end.

//--- stdin.txt
-1
-1
-1
-1
-1
70000
