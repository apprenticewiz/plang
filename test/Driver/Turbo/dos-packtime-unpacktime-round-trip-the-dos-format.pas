(*
Turbo Tier 4, Cluster C item 6: Dos.PackTime/UnpackTime convert a DateTime
record to/from the real, documented DOS 32-bit packed date/time format
(bits 0-4 seconds/2, 5-10 minutes, 11-15 hours, 16-20 day, 21-24 month,
25-31 year-1980) -- runtime/plang_dos.cpp's own plang_dos_packtime/
plang_dos_unpacktime.  Checked two ways: a real round trip through both
(Pack then Unpack recovers the original fields, seconds truncated to an
even number since the format only has 2-second resolution -- real
Borland/FPC field practice, not a bug), and the packed LongInt's own exact
bit pattern for one fixed, hand-computed date/time.
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t | FileCheck %s
*)

program DosPackTimeUnpackTime;
uses Dos;
var
  T: DateTime;
  P: LongInt;
begin
  T.Year := 2001; T.Month := 2; T.Day := 3;
  T.Hour := 4; T.Min := 5; T.Sec := 6;
  PackTime(T, P);
  { (2001-1980)=21 << 25 | 2 << 21 | 3 << 16 | 4 << 11 | 5 << 5 | (6 div 2)
    = 0x2A4320A3, computed independently in Python for this test. }
  Writeln('packed-hex: ', P = $2A4320A3);
  T.Year := 0; T.Month := 0; T.Day := 0; T.Hour := 0; T.Min := 0; T.Sec := 0;
  UnpackTime(P, T);
  Writeln('year: ', T.Year);
  Writeln('month: ', T.Month);
  Writeln('day: ', T.Day);
  Writeln('hour: ', T.Hour);
  Writeln('min: ', T.Min);
  Writeln('sec-even: ', T.Sec);
end.

(*
CHECK:packed-hex: TRUE
CHECK-NEXT:year: 2001
CHECK-NEXT:month: 2
CHECK-NEXT:day: 3
CHECK-NEXT:hour: 4
CHECK-NEXT:min: 5
CHECK-NEXT:sec-even: 6
*)
