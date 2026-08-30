(*
Turbo Tier 4, Cluster A item 0 is parsing-only: Parser::parseUnitFile
(ParseUnit.cpp) understands 'unit Name; interface ... end.', but nothing
downstream of parsing does yet -- no Sema, no CodeGen, no separate-
compilation artifact (Cluster A items 1-3's job).  An actual compile of a
standalone unit file must stop with a clear diagnostic rather than running
Sema/CodeGen against a ProgramNode whose real content (the UnitNode in
BareUnit) neither was ever taught about.  -dump-parse-tree still works on
the same file -- see test/Parse/ParserTurboUnits' own coverage of that.
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
CHECK: compiling a unit ('StandaloneUnit') is not yet supported
*)
