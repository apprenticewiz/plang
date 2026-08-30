(*
Turbo Tier 4, Cluster A item 0 was parsing-only: Parser::parseUnitFile
(ParseUnit.cpp) understands 'unit Name; interface ... end.', but nothing
downstream of parsing understood one yet.  Cluster A item 1 taught Sema what
a UnitNode is (Sema::checkUnit).  Cluster A item 2 replaces item 1's own
"type-checks, but stops there" placeholder (err_unit_compilation_not_yet_
supported) with real codegen: `plang -std=turbo -c` on a standalone unit
file now produces both a real object file (Codegen::emitUnit) and a
published interface file (its own .tui, written by buildTUIContent/
writeTUIFile in Frontend.cpp) -- genuine separate compilation, not a
placeholder.  See test/Sema/SemaTurboUnitScoping for coverage of what Sema
checks, test/Parse/ParserTurboUnits for -dump-parse-tree/-dump-ast on a unit
file, and Driver/Turbo/a-unit-compiled-standalone-can-be-used-by-a-program-
that-never-sees-its-source.pas for the full two-invocation separate-
compilation story (including the "delete the source" proof).

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/standaloneunit.pas -o %t.dir/standaloneunit.o
RUN: test -e %t.dir/standaloneunit.o
RUN: test -e %t.dir/standaloneunit.tui
RUN: FileCheck %s < %t.dir/standaloneunit.tui
*)

//--- standaloneunit.pas
unit StandaloneUnit;

interface

const Answer = 42;

implementation

end.

(*
CHECK: unit StandaloneUnit;
CHECK: interface
CHECK: const Answer = 42;
*)
