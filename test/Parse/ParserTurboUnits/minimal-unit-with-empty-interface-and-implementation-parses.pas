(*
Turbo Tier 4, Cluster A item 0: the smallest legal unit -- an interface
section exporting nothing and an implementation section declaring nothing,
with no initialization block at all.  Confirmed to compile under real
`fpc -Mtp` (no 'begin' required before 'end.' when there is no
initialization code).
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

unit MinimalUnit;

interface

implementation

end.

(*
CHECK:(unit MinimalUnit
CHECK-NEXT:  (interface
CHECK-NEXT:    ())
CHECK-NEXT:  (implementation
CHECK-NEXT:    ())
CHECK-NEXT:)
*)
