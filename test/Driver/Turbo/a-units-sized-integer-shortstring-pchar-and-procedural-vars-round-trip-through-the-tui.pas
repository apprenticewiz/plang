(*
Turbo Tier 4, Cluster A item 3: this locks in that typeDenoterToString
(Frontend.cpp, shared between the .pmi and .tui writers) already writes
every one of Turbo's sized-integer type names (Byte/Word/ShortInt/LongInt --
plain NamedTypeNode, resolved through Sema's own predeclared type aliases,
Sema.cpp's declareTypeAlias calls), PChar (also a NamedTypeNode, no
dialect-specific case needed), string[N] (StringTypeNode::IsShortString,
which already branched on Turbo's 'string[N]' vs EP's 'string(N)' before
this item), and a procedural type (ProcedureTypeNode, via
routineHeadingToString) correctly -- an empirical check per this item's own brief, since a prior
item's report's characterization of what was and was not covered here
needed independent verification rather than being taken on faith.
Confirmed: the type denoters round-trip fine on their own (the real bugs
this item found and fixed were in constant VALUE serialization, not type
serialization -- see this test's sibling tests for those). This test exists
mainly to keep the type-denoter side that way as a regression guard,
exercising a real -c/link/run cycle with the source deleted, the same as
its siblings.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/vartypesunit.pas -o %t.dir/vartypesunit.o
RUN: FileCheck --check-prefix=TUI %s < %t.dir/vartypesunit.tui
RUN: rm %t.dir/vartypesunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/vartypesunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
TUI: var VB: Byte;
TUI: var VW: Word;
TUI: var VS: ShortInt;
TUI: var VL: LongInt;
TUI: var VStr: string[40];
TUI: var VPC: PChar;
TUI: var VP: TProc;
*)

(*
CHECK:1
CHECK-NEXT:2
CHECK-NEXT:4
CHECK-NEXT:hello world
CHECK-NEXT:got 5
*)

//--- vartypesunit.pas
unit VarTypesUnit;

interface

type
  TProc = procedure(x: Integer);

var
  VB:   Byte;
  VW:   Word;
  VS:   ShortInt;
  VL:   LongInt;
  VStr: string[40];
  VPC:  PChar;
  VP:   TProc;

implementation

end.

//--- main.pas
program UsesVarTypesUnit;
uses VarTypesUnit;

procedure Show(x: Integer);
begin
  Writeln('got ', x);
end;

begin
  Writeln(SizeOf(VB));
  Writeln(SizeOf(VW));
  Writeln(SizeOf(VL));
  VStr := 'hello world';
  Writeln(VStr);
  VP := @Show;
  VP(5);
end.
