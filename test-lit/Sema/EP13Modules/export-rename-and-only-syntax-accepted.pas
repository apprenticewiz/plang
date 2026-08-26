(*
RUN: %plang_ep -dump-ast %s
*)

module MathUtil interface;
  export function Add(a: integer; b: integer): integer;
end.
program p;
begin end.
