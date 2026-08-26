(*
RUN: %plang -dump-ast %s
*)

program p(output);
function cmplx(x: integer): integer; begin cmplx := x end;
begin writeln(cmplx(1)) end.
