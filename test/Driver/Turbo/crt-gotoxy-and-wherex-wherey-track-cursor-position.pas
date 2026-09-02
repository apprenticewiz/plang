(*
Turbo Tier 4, Cluster C item 5 / issue #704: GotoXY emits the
ESC[<row>;<col>H cursor-position escape (row before column -- the ANSI/
VT100 convention, and the one real TP's own GotoXY(X, Y) matches too, X and
Y swapped from how they are written), and WhereX/WhereY read back a
tracked, window-relative cursor position (share/plang/units/Crt.pas,
runtime/plang_io.cpp/plang_file.cpp) that GotoXY sets directly AND that
ordinary Write/Writeln output also advances -- issue #704's own fix: this
used to be a pure state read of whatever GotoXY/ClrScr last set, ignoring
any Write in between, which disagreed with real TP/`fpc -Mtp` (confirmed:
real TP's own Crt reads the actual hardware cursor, which every write moves).
The first check below queries WhereX/WhereY with nothing written since
GotoXY, so it reads GotoXY's own target (12,7) unchanged; the first
Writeln's own trailing newline then puts the cursor at column 1 of the next
row (8), and the following Write('abc') must advance WhereX from there by
3 (to 4) before the second query -- issue #704's own fix; before it, this
still read back 12 unconditionally, ignoring the Write entirely. A GotoXY
outside the current (default 80x25) window is silently ignored, same as
real TP's own -- checked here by a third GotoXY(999, 999) that must NOT
move the cursor (the third query reads (1,9): unchanged since the second
Writeln's own trailing newline).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | tr '\033' 'E' | FileCheck %s
*)
program GotoXYWhereXY;
uses Crt;
var
  X, Y: Byte;
begin
  GotoXY(12, 7);
  X := WhereX; Y := WhereY;
  Writeln('at ', X, ',', Y);
  Write('abc'); { issue #704: must advance WhereX by 3, not leave it at 12 }
  X := WhereX; Y := WhereY;
  Writeln('at ', X, ',', Y);
  GotoXY(999, 999); { out of window -- must be a no-op }
  X := WhereX; Y := WhereY;
  Writeln('at ', X, ',', Y);
end.
(*
CHECK: E[7;12Hat 12,7
CHECK-NEXT: abcat 4,8
CHECK-NEXT: at 1,9
*)
