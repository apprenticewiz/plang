(*
'$' claims Turbo's grammar unconditionally (unlike EP's own nondecimal
literal, this never falls back to being read as anything else), so a '$'
with no hex digit following it is its own diagnostic rather than a generic
"unexpected character".
*)

(*
RUN: not %plang_ir -dump-tokens -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

$;

(*
CHECK: hexadecimal literal has no digits
*)
