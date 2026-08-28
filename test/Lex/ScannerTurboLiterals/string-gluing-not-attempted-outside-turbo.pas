(*
Gluing is Turbo-only: scanString's continued-gluing check starts with
`if (!Opts.turbo() ...) break;`, so outside -std=turbo a quoted string is
scanned exactly as it always was -- one fragment, no attempt to look past
its closing quote for more.  Here that means 'AB' is its own complete
StringLit, and the following '#' (unrecognized outside Turbo) and '65' and
'CD' are separate, mostly-broken tokens, matching the pre-Turbo baseline
exactly.  Default dialect is ISO 7185.
*)

(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

'AB'#65'CD'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "AB"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: IntLit "65"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: StringLit "CD"
*)
