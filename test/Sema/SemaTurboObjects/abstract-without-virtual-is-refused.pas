(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("Only virtual methods can be abstract") that 'abstract' with no 'virtual'
is a semantic error.  The parser (Parser::parseObjectMethodHeading) accepts
either order, or 'abstract' alone, on purpose -- this is Sema's rule to
enforce, not the grammar's (see that function's own comment, ParseType.cpp).
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program AbstractNotVirtual;

type
  TShape = object
    procedure Draw; abstract;
  end;

begin
end.

(*
CHECK: error: method 'Draw' is declared 'abstract' but not 'virtual'
*)
