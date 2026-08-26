(*
RUN: %plang_ep -dump-ast %s
*)

module M;
  function f(x: integer): integer;
  begin f := x end;
end.
program p;
  import M qualified;
begin end.
