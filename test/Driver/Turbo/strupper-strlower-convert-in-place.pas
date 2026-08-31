(*
Turbo Tier 4, Cluster C item 7's shipped `Strings` unit: StrUpper/StrLower --
real Borland/FPC field practice is an IN-PLACE conversion that also returns
the same pointer it was given (not a copy), checked here explicitly.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrUpper=HELLO WORLD
CHECK-NEXT:StrUpper-same-pointer=TRUE
CHECK-NEXT:StrLower=hello world
*)

program StrUpperLower;
uses Strings;
var
  Buf: array[0..20] of Char;
  PBuf, R: PChar;
begin
  Buf[0]:='H'; Buf[1]:='e'; Buf[2]:='l'; Buf[3]:='l'; Buf[4]:='o';
  Buf[5]:=' '; Buf[6]:='W'; Buf[7]:='o'; Buf[8]:='r'; Buf[9]:='l';
  Buf[10]:='d'; Buf[11]:=#0;
  PBuf := Buf;

  R := StrUpper(Buf);
  Writeln('StrUpper=', PBuf);
  Writeln('StrUpper-same-pointer=', R = PBuf);

  R := StrLower(Buf);
  Writeln('StrLower=', PBuf);
end.
