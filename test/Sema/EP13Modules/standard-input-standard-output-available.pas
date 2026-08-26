(*
RUN: %plang_ep -dump-ast %s
*)

program p;
  import StandardInput;
  import StandardOutput;
begin
  writeln('hello')
end.
