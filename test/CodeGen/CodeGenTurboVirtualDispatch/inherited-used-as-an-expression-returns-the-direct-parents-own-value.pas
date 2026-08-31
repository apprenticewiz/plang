(*
Issue #509: 'inherited' used to be parseable ONLY as a statement
(Parser::parseStatement's TokenKind::Inherited case, ParseStmt.cpp, building
an InheritedCallStmt) -- there was no expression-level production at all, so
'S := inherited Describe();' failed to parse ("expected expression, got
'inherited'") even though it is ordinary, legal Turbo Pascal object code.
This is the issue's own repro, verbatim: TDog.Describe calls 'inherited
Describe()' as part of building its OWN return value (concatenating the
ancestor's result with its own suffix), not merely as a bare statement whose
result is discarded the way every pre-existing Tier 5 test used it.

Cross-checked against a local `fpc -Mtp` build of this exact program: same
output, 'animal+dog'.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program inheritedexpr;
type
  TAnimal = object
    function Describe: string;
  end;
  TDog = object(TAnimal)
    function Describe: string;
  end;

function TAnimal.Describe: string;
begin
  Describe := 'animal';
end;

function TDog.Describe: string;
var
  S: string;
begin
  S := inherited Describe();
  Describe := S + '+dog';
end;

var
  D: TDog;
begin
  WriteLn(D.Describe());
end.

(*
CHECK:animal+dog
*)
