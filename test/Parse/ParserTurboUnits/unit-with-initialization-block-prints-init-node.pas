(*
Mirror of the sibling no-init test: a unit whose implementation section IS
followed by 'begin ... end' gets a non-null UnitNode::InitBody, which the
printer shows as a trailing '(init ...)' node.  parseCompoundStmt() itself
consumes the closing 'end' here -- there is no second, separate 'end' for
the unit itself to expect afterwards (ParseUnit.cpp's own comment on why
the no-init-block branch calls expect(End) but this one does not).
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

unit WithInit;

interface

implementation

var Scratch: Integer;

begin
  Scratch := 0;
end.

(*
CHECK:(unit WithInit
CHECK-NEXT:  (interface
CHECK-NEXT:    ())
CHECK-NEXT:  (implementation
CHECK-NEXT:    (var (Scratch) Integer)
CHECK-NEXT:    ())
CHECK-NEXT:  (init (compound
CHECK-NEXT:    (assign Scratch 0)))
CHECK-NEXT:)
*)
