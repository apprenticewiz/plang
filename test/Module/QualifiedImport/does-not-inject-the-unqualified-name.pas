(*
RUN: not %plang -std=iso10206 %s -o %t
*)

module m1;
  function fetch: integer; begin fetch := 5 end;
end.
program p;
  import m1 qualified;
begin writeln(fetch()) end.
