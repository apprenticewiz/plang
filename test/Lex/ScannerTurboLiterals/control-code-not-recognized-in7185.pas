(*
Outside -std=turbo, '#' still has no dispatch arm anywhere and falls to
scanSymbol's default "unexpected character" case; scanning then resumes at
the digits, which scan as an ordinary decimal IntLit since nothing before
them makes EP's own nondecimal-literal path apply.  Default dialect is
ISO 7185.
*)

(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

#65

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "65"
*)
