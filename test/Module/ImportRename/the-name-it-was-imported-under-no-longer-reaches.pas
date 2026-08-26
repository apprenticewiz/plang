(*
RUN: not %plang -std=iso10206 %s -o %t
*)

module m;
  function f(x: integer): integer; begin f := x + 1 end;
end.
program p;
  import m (f => plus1);
begin writeln(f(10)) end.
