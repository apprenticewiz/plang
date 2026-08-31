(*
Turbo Tier 4, Cluster C item 5: GotoXY emits the ESC[<row>;<col>H cursor-
position escape (row before column -- the ANSI/VT100 convention, and the
one real TP's own GotoXY(X, Y) matches too, X and Y swapped from how they
are written), and WhereX/WhereY read back exactly the window-relative
coordinates GotoXY itself set -- CursorX/CursorY, this unit's own private
state (share/plang/units/Crt.pas), never queried from the real terminal
(confirmed real TP's own WhereX/WhereY never do either: they are pure state
reads). A GotoXY outside the current (default 80x25) window is silently
ignored, same as real TP's own -- checked here by a second GotoXY(999, 999)
that must NOT move the cursor.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | tr '\033' 'E' | FileCheck %s
*)
program GotoXYWhereXY;
uses Crt;
begin
  GotoXY(12, 7);
  Writeln('at ', WhereX, ',', WhereY);
  GotoXY(999, 999); { out of window -- must be a no-op }
  Writeln('at ', WhereX, ',', WhereY);
end.
(*
CHECK: E[7;12Hat 12,7
CHECK-NEXT: at 12,7
*)
