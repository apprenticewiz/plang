(*
Turbo Tier 4, Cluster C item 7: the real, shipped `Printer` unit
(share/plang/units/Printer.pas) -- its own header comment has the full
account of why Lst has to be bound by a C++ global constructor in
runtime/plang_printer.cpp rather than a Pascal init section, and the real
`fpc` field practice this matches (Lst auto-bound to a plain temp file, NOT
a live lpr/CUPS pipe -- CI-safe by construction: nothing here ever talks to
a print spooler).

This proves the auto-bind itself: a program that does nothing but `uses
Printer` and immediately writes to Lst needs no Assign/Rewrite of its own
first, exactly like real Borland/FPC.  PLANG_LST_PATH pins the otherwise
PID-keyed default path so this RUN line can find and FileCheck the result
deterministically -- see plang_printer.cpp's own header comment for why that
override exists and what it does and does not change about the real binding
mechanism.

RUN: rm -f %t.lst
RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: env PLANG_LST_PATH=%t.lst %run %t
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.lst
*)

(*
CHECK:Hello from Lst
CHECK-NEXT:42
*)

program AutoBoundLst;
uses Printer;
begin
  Writeln(Lst, 'Hello from Lst');
  Writeln(Lst, 42);
  Close(Lst);
end.
