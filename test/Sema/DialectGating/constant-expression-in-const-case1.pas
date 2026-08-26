(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
const c = 1 < 2;
begin writeln(c) end.
