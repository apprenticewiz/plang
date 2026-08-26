(*
RUN: %plang_ep -dump-ast %s
*)

module M;
  function f(x: integer): integer;
  begin f := x end;
  to begin do writeln('init');
  to end do writeln('done');
end.
program p;
  import M;
begin end.
