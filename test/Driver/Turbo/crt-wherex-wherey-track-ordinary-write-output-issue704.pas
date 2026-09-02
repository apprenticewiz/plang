(*
Issue #704: WhereX/WhereY used to be a pure state read of whatever
GotoXY/ClrScr last set, ignoring any ordinary Write/Writeln in between --
`GotoXY(5, 3); Write('abc'); Writeln(WhereX, WhereY)` reported 5,3 (GotoXY's
own target, unmoved), where real TP/`fpc -Mtp` (which reads the real
hardware cursor -- every write moves it) reports 8,3 (GotoXY's own target
PLUS the 3 characters just written).  The fix tracks a software cursor in
the runtime (runtime/plang_io.cpp's plangCrtTrackOutput, called from both
plang_io.cpp's own plangOutN and plang_file.cpp's turbo char/string file
writers -- see plangCrtTrackOutput's own comment for why a plain
`Write`/`Writeln` needs the SECOND call site, not just the first, under
-std=turbo), synced to GotoXY/ClrScr's own target via the CrtSyncCursor
builtin and advanced for ordinary text by plangCrtTrackOutput itself.

Four things checked, matching the issue's own repro plus a wrap-to-next-row
case (a Write containing a newline mid-string) and an ANSI/SGR-escape case:
  1. GotoXY(5, 3) then Write('abc') then WhereX/WhereY: 8,3.
  2. A Writeln (its own trailing newline) resets WhereX to 1 and advances
     WhereY by 1: 1,4.
  3. A Write embedding an explicit newline character (Chr(10)) behaves the
     same as Writeln's own trailing one: WhereX resets to 1, WhereY
     advances -- 'xy' after the embedded newline leaves WhereX at 3, one
     row further than the previous check's own WhereY.
  4. TextColor's own ApplyAttr (share/plang/units/Crt.pas) writes an ANSI
     SGR escape sequence through the exact same Write path -- that escape's
     OWN bytes (digits, semicolons, 'm') must NOT be counted as ordinary
     text advancing the column, or WhereX would come back wildly wrong
     after every single TextColor/TextBackground/ClrScr call, not just an
     unadorned Write.  GotoXY(2, 2), TextColor(Red), Write('hi') must read
     back 4,2 (2 + 2 characters), not something inflated by the SGR
     escape's own byte count.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)
program WhereXYTracksWrite;
uses Crt;
var
  X, Y: Byte;
begin
  GotoXY(5, 3);
  Write('abc');
  X := WhereX; Y := WhereY;
  Writeln(X, ' ', Y);

  Writeln('line');
  X := WhereX; Y := WhereY;
  Writeln(X, ' ', Y);

  Write('a', Chr(10), 'xy');
  X := WhereX; Y := WhereY;
  Writeln(X, ' ', Y);

  GotoXY(2, 2);
  TextColor(Red);
  Write('hi');
  X := WhereX; Y := WhereY;
  Writeln(X, ' ', Y);
end.
(*
CHECK: 8 3
CHECK: 1 5
CHECK: 3 7
CHECK: 4 2
*)
