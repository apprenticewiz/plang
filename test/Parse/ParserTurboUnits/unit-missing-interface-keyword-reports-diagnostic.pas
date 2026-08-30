(*
The 'interface' keyword is required immediately after the unit heading --
a unit that jumps straight to 'implementation' is a syntax error, not a
unit with an empty (and so omittable) interface section.  Confirmed against
real 'fpc -Mtp', which refuses the same shape with its own
"'INTERFACE' expected" diagnostic.
*)

(*
RUN: not %plang_ir -std=turbo -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

unit NoInterfaceKeyword;

implementation

end.

(*
CHECK: expected 'interface', got 'implementation'
*)
