(*
Driver.cpp regression gate, not just Frontend.cpp's: the top-level `plang`
driver builds its own Options::rangeChecks (Driver.h) from -frange-checks/
-fno-range-checks and used to forward "-fno-range-checks" to -pc1 only
when that field was false, saying NOTHING (no flag at all) when it looked
true -- which was ALSO its own untouched, dialect-blind default, so an
explicit -frange-checks given alongside -std=turbo was indistinguishable
from the user having said nothing, and -pc1 fell back to Turbo's own
off-by-default the same as if -frange-checks had never been typed.
Options::rangeChecks is std::optional<bool> now precisely so
makeFEArgs can tell "explicitly asked for on" apart from "default", and
forwards one of the two flags explicitly either way -- this proves the
explicit case actually reaches -pc1 and takes effect, through the real
`plang` binary (not %plang_ir or any other IR-only substitution), the
same subrange-assignment shape the sibling default-off/explicit-{$R+}
tests use.
*)

(*
RUN: %plang -std=turbo -frange-checks %s -o %t
RUN: %checkexit 201 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 201 at $
*)

program explicitfrangechecks;
var
  s: 1..10;
  i: Integer;
begin
  i := 500;
  s := i;
  writeln('unreachable: ', s);
end.
