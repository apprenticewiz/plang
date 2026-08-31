(*
Turbo Tier 4, Cluster C item 7's shipped `Strings` unit: StrLCopy/StrLCat,
the bounded forms of StrCopy/StrCat.  Real Borland/FPC field practice,
confirmed and hand-implemented in runtime/plang_strings.cpp rather than
delegated to strncpy(3)/strncat(3) (whose own truncation/padding/termination
rules do not match): StrLCopy copies at most MaxLen characters and ALWAYS
null-terminates the result; StrLCat bounds the RESULT's total length to
MaxLen (not just the number of characters appended) and also always
null-terminates.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrLCopy(3)=Hel
CHECK-NEXT:StrLCat(4)=HiHe
CHECK-NEXT:StrLCat(no-op)=Already
*)

program StrLCopyLCat;
uses Strings;
var
  Src, Dst: array[0..40] of Char;
  PDst: PChar;
begin
  Src[0]:='H'; Src[1]:='e'; Src[2]:='l'; Src[3]:='l'; Src[4]:='o'; Src[5]:=#0;

  StrLCopy(Dst, Src, 3);
  PDst := Dst;
  Writeln('StrLCopy(3)=', PDst);

  Dst[0]:='H'; Dst[1]:='i'; Dst[2]:=#0;
  StrLCat(Dst, Src, 4);
  PDst := Dst;
  Writeln('StrLCat(4)=', PDst);

  // MaxLen already met or exceeded by Dst's own existing length: StrLCat is
  // a no-op, not a further truncation of what is already there.
  Dst[0]:='A'; Dst[1]:='l'; Dst[2]:='r'; Dst[3]:='e'; Dst[4]:='a';
  Dst[5]:='d'; Dst[6]:='y'; Dst[7]:=#0;
  StrLCat(Dst, Src, 4);
  PDst := Dst;
  Writeln('StrLCat(no-op)=', PDst);
end.
