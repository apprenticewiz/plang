(*
Turbo Tier 4, Cluster C item 7's shipped `Strings` unit: StrPos (substring
search, real strstr(3) semantics -- nil when absent), StrScan (first
occurrence of a character, real strchr(3) semantics) and StrRScan (last
occurrence, real strrchr(3) semantics).  Every found pointer is checked by
its OFFSET from the start of the searched string (pointer arithmetic already
proven working by Tier 2's own PChar landing), not just its dereferenced
character, so this also exercises that a Strings-unit function's PChar
RESULT is the exact same runtime pointer, not a copy.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrPos(ll)=2
CHECK-NEXT:StrPos(absent)=NIL
CHECK-NEXT:StrScan(l)=2
CHECK-NEXT:StrRScan(l)=3
*)

program StrPosScanRScan;
uses Strings;
var
  Hay, Needle: array[0..20] of Char;
  PHay, P: PChar;
begin
  Hay[0]:='H'; Hay[1]:='e'; Hay[2]:='l'; Hay[3]:='l'; Hay[4]:='o'; Hay[5]:=#0;
  PHay := Hay;

  Needle[0]:='l'; Needle[1]:='l'; Needle[2]:=#0;
  P := StrPos(Hay, Needle);
  Writeln('StrPos(ll)=', P - PHay);

  Needle[0]:='z'; Needle[1]:=#0;
  P := StrPos(Hay, Needle);
  if P = nil then
    Writeln('StrPos(absent)=NIL')
  else
    Writeln('StrPos(absent)=FOUND');

  P := StrScan(Hay, 'l');
  Writeln('StrScan(l)=', P - PHay);

  P := StrRScan(Hay, 'l');
  Writeln('StrRScan(l)=', P - PHay);
end.
