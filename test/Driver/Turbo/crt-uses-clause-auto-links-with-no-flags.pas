(*
Turbo Tier 4, Cluster C item 5: `uses Crt;` has to work with NO flags at
all, the same "no -I, no PLANG_UNIT_DIR, no explicit .o" experience Cluster
B item 4's own InstallProbeUnit CI proof gives a const-only unit -- but Crt
exports real procedures with real bodies (ClrScr and friends), so a program
that `uses Crt` needs an actual crt.o LINKED in, not just crt.tui/Crt.pas to
type-check against.  Before this item, separate compilation only worked
with the resulting .o named explicitly on the command line (every existing
sibling test in this directory that links a separately-compiled unit does
exactly that -- see e.g. a-units-sized-integer-shortstring-pchar-and-
procedural-vars-round-trip-through-the-tui.pas's own final %plang line).
lib/Driver/Driver.cpp's own scanUsesClauseUnitNames/findShippedUnitObject
close that gap: the driver now auto-discovers and links a `uses`d unit's own
shipped object file the same way Sema::loadUnitInterfaceExports auto-
discovers its .tui/.pas, using the identical unitSearchPaths() tiers -- so
this is a single, ordinary, no-extra-anything %plang invocation, exercising
the SAME build-tree PLANG_UNIT_DIR fallback tier the CI install-rules step
exercises for real from an installed prefix (see this item's own report for
that half of the proof).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | tr '\033' 'E' | FileCheck %s
*)
program UsesCrtWithNoFlags;
uses Crt;
begin
  Writeln('linked ok');
  GotoXY(1, 1); { Crt.pas's own EnsureInit runs on the first CALL into the
                  unit -- see its header comment on why a bare read of an
                  exported var like TextAttr, with no prior call, would not
                  trigger it }
  Writeln('attr=', TextAttr);
end.
(*
CHECK: linked ok
CHECK-NEXT: E[1;1Hattr=7
*)
