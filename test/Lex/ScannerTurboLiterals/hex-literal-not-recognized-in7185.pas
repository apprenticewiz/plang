(*
Outside -std=turbo, '$' still has no dispatch arm anywhere -- it falls to
scanSymbol's default case exactly as it did before this feature existed, an
"unexpected character" error, and scanning resumes at the digits as an
ordinary decimal IntLit... except here they aren't decimal digits, so they
scan as a plain identifier instead.  Default dialect is ISO 7185.
*)

(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

$FF

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "FF"
*)
