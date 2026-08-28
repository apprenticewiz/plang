(*
A hex digit run wide enough to overflow int64_t is rejected with its own
diagnostic, the same overflow-before-the-multiply guard EP's nondecimal
literal above it in Scanner.cpp uses and for the same reason: once Value has
wrapped there is no recovering the true magnitude from it.
*)

(*
RUN: not %plang_ir -dump-tokens -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

$FFFFFFFFFFFFFFFFF

(*
CHECK: hexadecimal literal is out of range
*)
