(*
RUN: %plang_ep -dump-ast %s
*)

(* issue #410 contrast case: a 'var' parameter aliases the caller's own
   storage instead of copying the actual into a local of its own, so no
   oversized alloca is ever materialized for it in CodeGen -- unlike a
   by-value parameter or a function result, #223/#410's size gate must NOT
   reject this declaration. *)

program t;
procedure p(var s: string(maxint));
begin
  s := 'x'
end;
begin
end.
