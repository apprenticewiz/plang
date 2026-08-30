(*
Turbo Tier 4, Cluster A item 1: unlike a unit's own 'uses' clause (Cluster A
item 0, ParseUnit.cpp's own parseUsesClause), an ORDINARY PROGRAM had no
'uses' clause of its own at all before this item -- Parser::parseProgram
never even checked for the token.  This is the parser half of fixing that:
'uses' identifier-list ';' right after the program heading (or, under
Turbo's own no-heading form, right at the top of the file), before the
block's own declarations.

RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program UsesDemo;

uses SysUtils, Strings;

begin
end.

(*
CHECK:(program UsesDemo (uses SysUtils Strings)
CHECK-NEXT:  (compound))
*)
