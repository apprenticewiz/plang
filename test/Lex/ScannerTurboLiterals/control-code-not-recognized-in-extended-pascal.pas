(*
Same as control-code-not-recognized-in7185.pas, under Extended Pascal: a
bare '#' with no leading decimal digit run before it never reaches EP's own
nondecimal-literal code in scanNumber (that only fires once a digit run has
already been consumed), so it is still just an unrecognized character here.
*)

(*
RUN: not %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

#65

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: IntLit "65"
*)
