(*
Turbo Tier 5, Cluster A item 0: every token this item adds --
'object'/'constructor'/'destructor' (already reserved before this item) and
the newly reserved 'virtual'/'abstract'/'private'/'public' -- is a
DIALECT_KEYWORD gated to TP alone (TokenKinds.def), so outside -std=turbo
the scanner hands each straight back as an ordinary identifier and a
program is free to use any of them as a variable name.  No dialect flag at
all here: this is the default ISO 7185 dialect, the strictest case.
*)

(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program NotTurbo;
var
  object, virtual, private, public, abstract: integer;
begin
  object := 1;
  virtual := 2;
  private := 3;
  public := 4;
  abstract := 5
end.

(*
CHECK:(program NotTurbo
CHECK-NEXT:  (var (object virtual private public abstract) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign object 1)
CHECK-NEXT:    (assign virtual 2)
CHECK-NEXT:    (assign private 3)
CHECK-NEXT:    (assign public 4)
CHECK-NEXT:    (assign abstract 5)))
*)
