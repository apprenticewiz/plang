(*
Turbo Tier 4, Cluster A item 0 was parsing-only: Parser::parseUnitFile
(ParseUnit.cpp) understands 'unit Name; interface ... end.', but nothing
downstream of parsing understood one yet.  Cluster A item 1 taught Sema what
a UnitNode is (Sema::checkUnit) -- a standalone unit file's own mistakes are
now reported at their own location instead of this diagnostic firing
unconditionally -- but CodeGen for a unit compiled as its own separate
translation unit (real object-file emission with cross-unit linkage) is
still item 2/3's job, so a unit Sema accepts cleanly still stops here rather
than producing an object file.  See test/Sema/SemaTurboUnitScoping for
coverage of what Sema now actually checks, and
test/Parse/ParserTurboUnits for -dump-parse-tree/-dump-ast on a unit file.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

unit StandaloneUnit;

interface

implementation

end.

(*
CHECK: 'StandaloneUnit' type-checks, but compiling a unit to an object file is not yet supported
*)
