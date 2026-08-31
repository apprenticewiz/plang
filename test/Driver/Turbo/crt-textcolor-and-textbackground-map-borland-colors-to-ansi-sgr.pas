(*
Turbo Tier 4, Cluster C item 5: TextColor/TextBackground pack Borland's own
TextAttr byte (bits 0-3 foreground 0-15, bits 4-6 background 0-7, bit 7
blink) and ApplyAttr (share/plang/units/Crt.pas) converts it to ANSI SGR --
confirmed digit-for-digit against fpc's own unix/crt.pp AnsiTbl constant
('04261537'): Borland's own color ORDER (Black,Blue,Green,Cyan,Red,Magenta,
Brown,LightGray) is NOT ANSI's (Black,Red,Green,Yellow,Blue,Magenta,Cyan,
White), so this is not simple arithmetic. Yellow (14, >7) sets the ';1'
high-intensity bit and maps its low 3 bits (6, Brown's own base color) to
ANSI 33; Blue (1) maps to ANSI blue 34 (not 31, which is where Blue would
land if Borland's own index were read as an ANSI index directly -- the
whole point of this test). TextBackground(Red + Blink) exercises the blink
bit (';5') a background color can carry.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | tr '\033' 'E' | FileCheck %s
*)
program TextColorMapping;
uses Crt;
begin
  TextColor(Yellow);
  Writeln;
  TextBackground(Blue);
  Writeln;
  TextBackground(Red + Blink);
  Writeln;
end.
(*
CHECK: E[0;1;33;40m
CHECK-NEXT: E[0;1;33;44m
CHECK-NEXT: E[0;1;33;41;5m
*)
