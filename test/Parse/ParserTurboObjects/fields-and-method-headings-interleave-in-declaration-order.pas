(*
Turbo Tier 5, Cluster A item 0: parsing only.  Real Turbo Pascal freely
interleaves field declarations and method headings inside one object-type
member list; a future Sema layout pass needs that exact order (a field's
offset depends on what was declared before it), so ObjectTypeNode::Members
is one ordered, tagged list (ObjectMember) rather than separate
Fields/Methods vectors.  This checks the parser preserves that order.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program Interleave;

type
  TThing = object
    A: integer;
    procedure DoA;
    B: integer;
    function GetB: integer;
  end;

begin
end.

(*
CHECK:(program Interleave
CHECK-NEXT:  (typedef TThing (object (public (A integer)) (public procedure DoA ()) (public (B integer)) (public function GetB () integer)))
CHECK-NEXT:  (compound))
*)
