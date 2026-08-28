(*
'$' stays unrecognized under Extended Pascal too, unaffected by (and not
interacting with) EP's own '16#FF' nondecimal-base literal, which is a
completely different character and code path (scanNumber, only after a
leading decimal digit run).
*)

(*
RUN: not %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

$FF

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "FF"
*)
