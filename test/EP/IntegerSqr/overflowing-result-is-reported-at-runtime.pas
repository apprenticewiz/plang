(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: no representable result
*)

(* issue #219: 3037000500 is just over sqrt(2^63), so squaring it overflows
   int64_t.  Unguarded, plang_sqr_int's X*X was signed-overflow UB that in
   practice silently wrapped to a negative value instead of trapping the way
   plang_abs_int and plang_ipow already do for their own overflow cases. *)
program p(output); var x: integer;
begin x := 3037000500; writeln(sqr(x)) end.
