(*
'uses' must be the very first thing in its section -- confirmed against
real 'fpc -Mtp', which refuses a 'uses' clause written after a declaration
with its own "'IMPLEMENTATION' expected but 'USES' found" (the parser
having already committed to reading declarations, 'uses' looks like
nothing legal a declaration section can contain, so the section is taken
to be over and the next section keyword is expected instead).
Parser::parseUnitFile only ever calls parseUsesClause() once, before
entering parseUnitDeclarations at all, so it has exactly this same blind
spot -- this test pins that down as intentional, not an oversight.
*)

(*
RUN: not %plang_ir -std=turbo -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

unit UsesAfterDecl;

interface

type T = Integer;

uses SysUtils;

implementation

end.

(*
CHECK: expected 'implementation', got 'uses'
*)
