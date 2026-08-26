(*
RUN: not %plang_ep -dump-ast %s
*)

module M interface;
  export M = (f..g);
  function f: integer;
  function g: integer;
end.
program p;
begin end.
