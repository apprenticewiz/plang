(*
Turbo Tier 4, Cluster C item 7: the real, shipped `Strings` unit
(share/plang/units/Strings.pas) -- not a throwaway stand-in -- resolved
against PLANG_UNIT_DIR the same way
a-uses-clause-resolves-against-plang-unit-dir-with-no-i-flag.pas already
proves the general mechanism.  This exercises the three most basic PChar
operations: StrLen (Cardinal length of a null-terminated string), StrCopy
(whole-string copy, real strcpy(3) semantics) and StrCat (whole-string
append, real strcat(3) semantics) -- all backed by runtime/plang_strings.cpp,
not anything this unit's own (deliberately empty) implementation section
could produce; see that file's own header comment for why.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrLen=5
CHECK-NEXT:StrCopy=Hello
CHECK-NEXT:StrCat=HelloWorld
*)

program StrLenCopyCat;
uses Strings;
var
  Src, Dst: array[0..40] of Char;
  PDst: PChar;
begin
  Src[0]:='H'; Src[1]:='e'; Src[2]:='l'; Src[3]:='l'; Src[4]:='o'; Src[5]:=#0;
  Writeln('StrLen=', StrLen(Src));

  StrCopy(Dst, Src);
  PDst := Dst;
  Writeln('StrCopy=', PDst);

  Dst[0]:='W'; Dst[1]:='o'; Dst[2]:='r'; Dst[3]:='l'; Dst[4]:='d'; Dst[5]:=#0;
  StrCat(Src, Dst);
  PDst := Src;
  Writeln('StrCat=', PDst);
end.
