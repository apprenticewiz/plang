(*
Turbo Tier 5, Cluster A item 3: confirmed against a local fpc -Mtp build
that 'A.Speak;' with no parens at all is legal Pascal call syntax for a
zero-argument method, exactly the same relaxation a bare 'Foo;' already
gets for a zero-argument ordinary procedure.  Parser::parseStatement builds
this from a plain FieldExpr Lval whose Field turns out to name a method
(MethodCallStmt with an empty Args list) -- see that function's own comment
(ParseStmt.cpp).
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program BareMethodCallStatement;

type
  TAnimal = object
    procedure Speak;
  end;

procedure TAnimal.Speak;
begin
end;

var
  A: TAnimal;
begin
  A.Speak;
end.

(*
CHECK: (methodcall A Speak)
*)
