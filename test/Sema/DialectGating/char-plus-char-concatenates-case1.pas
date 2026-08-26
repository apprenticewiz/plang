(*
RUN: %plang_ep -dump-ast %s
*)

program p(output);
begin writeln('a' + 'b') end.
