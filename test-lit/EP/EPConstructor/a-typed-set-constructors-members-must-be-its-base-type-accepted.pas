(*
checkSetLit ignored the constructor's TYPE NAME and derived the type from
its ELEMENTS, so `cs['x', 300]` for a `set of col` was accepted and
produced the empty set -- while the untyped `['x']` in the same context
IS caught, so the two spellings of one construct disagreed about whether
the program was legal.
*)

(* And the legal ones still work, including a range. *)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false true
*)

program p(output);
type col = (red, green, blue); cs = set of col; chs = set of char;
var s: cs; c: chs;
begin s := cs[red, blue]; c := chs['a'..'c'];
  writeln(red in s, ' ', green in s, ' ', ('b' in c)) end.
