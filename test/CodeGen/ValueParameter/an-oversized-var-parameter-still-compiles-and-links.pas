(*
RUN: %plang -std=iso10206 %s -o %t
*)

(* issue #410 contrast case: #223's local/global size gate, extended by #410
   to by-value parameters and function results, must NOT reject a 'var'
   parameter of the same oversized type -- it aliases the caller's own
   storage instead of copying into a fixed-size local `alloca`, so CodeGen
   never materializes the huge object this gate exists to catch, and the
   full compile-and-link pipeline (not just -dump-ast) must keep succeeding. *)

program t;
procedure p(var s: string(maxint));
begin
  s := 'x'
end;
begin
end.
