(*
The sized-integer ladder's own landing test (sized-integer-ladder-and-
ansichar-declare-and-hold-correct-boundary-values.pas, test/Driver/Turbo/)
drives every rung's write path to both ends of its declared range, but never
exercises read() for anything past ShortInt/Byte.  This is the read-side
complement: every remaining rung (SmallInt, Word, LongInt, Cardinal,
LongWord, Int64) read from stdin at both ends of its range, plus AnsiChar,
each written straight back out to confirm the value that was read is the
value that was meant.  QWord has its own dedicated test
(qword-write-and-read-round-trip-the-full-unsigned-range.pas) for the one
rung whose read path needed more than emitReadArg's existing width-
conversion machinery already provided.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:sm=-32768
CHECK-NEXT:sm=32767
CHECK-NEXT:wd=0
CHECK-NEXT:wd=65535
CHECK-NEXT:li=-2147483648
CHECK-NEXT:li=2147483647
CHECK-NEXT:cd=0
CHECK-NEXT:cd=4294967295
CHECK-NEXT:lw=0
CHECK-NEXT:lw=4294967295
CHECK-NEXT:i6=-9223372036854775807
CHECK-NEXT:i6=9223372036854775807
CHECK-NEXT:ac=A
*)

//--- test.pas
var
  sm: SmallInt;
  wd: Word;
  li: LongInt;
  cd: Cardinal;
  lw: LongWord;
  i6: Int64;
  ac: AnsiChar;
begin
  readln(sm); writeln('sm=', sm);
  readln(sm); writeln('sm=', sm);
  readln(wd); writeln('wd=', wd);
  readln(wd); writeln('wd=', wd);
  readln(li); writeln('li=', li);
  readln(li); writeln('li=', li);
  readln(cd); writeln('cd=', cd);
  readln(cd); writeln('cd=', cd);
  readln(lw); writeln('lw=', lw);
  readln(lw); writeln('lw=', lw);
  readln(i6); writeln('i6=', i6);
  readln(i6); writeln('i6=', i6);
  readln(ac); writeln('ac=', ac);
end.

//--- stdin.txt
-32768
32767
0
65535
-2147483648
2147483647
0
4294967295
0
4294967295
-9223372036854775807
9223372036854775807
A
