(*
RUN: %plang_ep -dump-ast %s
*)

module M interface;
  export A = (f);
  export B = (g);
  function f: integer;
  function g: integer;
end.
program p;
begin end.
