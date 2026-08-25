(*
checkSetLit ignored the constructor's TYPE NAME and derived the type from
its ELEMENTS, so `cs['x', 300]` for a `set of col` was accepted and
produced the empty set -- while the untyped `['x']` in the same context
IS caught, so the two spellings of one construct disagreed about whether
the program was legal.
*)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign 'char'
*)

program p(output);
type col = (red, green, blue); cs = set of col;
var s: cs;
begin s := cs['x', 300]; writeln(red in s) end.
