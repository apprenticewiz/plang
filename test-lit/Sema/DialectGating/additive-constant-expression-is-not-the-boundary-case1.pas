(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
const c = 2 + 3;
begin writeln(c) end.
