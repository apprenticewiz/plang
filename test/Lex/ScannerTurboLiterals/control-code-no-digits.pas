(*
A bare '#' (or '#$') with no digit following it is its own diagnostic, not a
generic "unexpected character" -- Turbo's '#' claims the grammar
unconditionally, matching $hex's own no-digits diagnostic.
*)

(*
RUN: not %plang_ir -dump-tokens -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

#;

(*
CHECK: control-character code has no digits
*)
