(*
Issue #790: a unit named in a 'uses' clause that has NEVER been compiled
with `-c` (no .tui published anywhere, so Sema::loadUnitInterfaceExports
can only resolve it through its .pas source re-parse fallback) has no
guaranteed matching .o in this link.  Referencing only its foldable
INTERFACE constant already worked before this issue (constants are
inlined, not a runtime symbol), and this test's whole point is that it
STILL works after this change: Sema::unitInitCallNames drops a
fallback-resolved unit rather than have CodeGen emit an unconditional call
to an `__plang_init_<name>` the linker could never resolve (there is no
object file for FallbackUnit anywhere on this command line, on purpose).
A regression here would turn a previously-working, link-everything-but-
this-one-unit's-object build into a fresh, surprising link failure.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:answer=42
*)

//--- fallbackunit.pas
unit FallbackUnit;

interface

const Answer = 42;

implementation

begin
  Writeln('FallbackUnit init ran -- this must never print');
end.

//--- main.pas
program FallbackProg;
uses FallbackUnit;
begin
  Writeln('answer=', Answer);
end.
