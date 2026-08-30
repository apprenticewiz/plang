(*
A unit whose implementation section declares something but has no
initialization code at all runs straight from its last declaration into
'end.' -- no empty 'begin end' placeholder is required (confirmed against
real 'fpc -Mtp').  UnitNode::InitBody stays null, and the printer (which
only emits an '(init ...)' node when InitBody is set) leaves it out
entirely -- contrast the sibling test that does have one.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

unit NoInit;

interface

implementation

var Scratch: Integer;

end.

(*
CHECK:(unit NoInit
CHECK-NEXT:  (interface
CHECK-NEXT:    ())
CHECK-NEXT:  (implementation
CHECK-NEXT:    (var (Scratch) Integer)
CHECK-NEXT:    ())
CHECK-NEXT:)
*)
