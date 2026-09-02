(*
Turbo Tier 4, Cluster C item 5: Window(X1,Y1,X2,Y2) packs WindMin/WindMax
exactly the way real TP's own does -- WindMin=(Y1-1) shl 8+(X1-1),
WindMax=(Y2-1) shl 8+(X2-1) (confirmed against fpc's own unix/crt.pp
Window()) -- and every coordinate GotoXY/WhereX/WhereY/ClrScr/ClrEol use
afterward is WINDOW-relative, offset by that origin when it reaches the
real terminal.  A sub-window (not the full 80x25 default) also takes
ClrScr/ClrEol off their full-screen ESC[2J/ESC[K fast path: ClrScr clears
row-by-row with ESC[<n>X, and ClrEol -- since the window's own right edge
here is short of column 80 -- clears only to the window's own right edge,
also via ESC[<n>X, rather than ESC[K (which would clear to the REAL
terminal's line end, past the window).

Issue #704: WhereX/WhereY now also track ordinary Write/Writeln output (not
just GotoXY/ClrScr), so the WindMin/WindMax Writeln just below -- 14
characters, written from the window-relative position Window()'s own
trailing GotoXY(1,1) set -- itself moves the tracked cursor before the
explicit GotoXY(1, 1) right after it resyncs back to (1,1); WhereX/WhereY
are read into locals immediately after THAT (nothing written in between),
so this checks the resync lands on the window-relative (1,1) Window()
itself asks for, not the absolute (10,5) the real terminal was actually
sent -- the whole point of "every coordinate is WINDOW-relative" above.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | tr '\033' 'E' | FileCheck %s
*)
program WindowSubRegion;
uses Crt;
var
  X, Y: Byte;
begin
  Window(10, 5, 20, 8); { 11 columns x 4 rows, origin (10,5) }
  Writeln('win=', WindMin, ',', WindMax);
  GotoXY(1, 1);
  X := WhereX; Y := WhereY;
  Writeln('at ', X, ',', Y);
  ClrEol;
end.
(*
CHECK: E[5;10Hwin=1033,1811
CHECK-NEXT: E[5;10Hat 1,1
CHECK-NEXT: E[0;37;40mE[11X
*)
