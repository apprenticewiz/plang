(*
RUN: %plang_ep -dump-ast %s
*)

module IO(input, output) interface;
  export function readline(var s: string(80)): integer;
end.
program p;
begin end.
