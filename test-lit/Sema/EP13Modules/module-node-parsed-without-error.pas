(*
RUN: %plang_ep -dump-ast %s
*)

module M interface;
  export function Scale(x: real; k: integer): real;
end.
program p;
begin end.
