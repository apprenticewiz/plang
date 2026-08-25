(*
RUN: not %plang -std=iso10206 %s -o %t
*)

module m interface;
  export m = (internalSquare => square);
  function internalSquare(x: integer): integer;
end.
module m;
  function internalSquare(x: integer): integer;
  begin internalSquare := x * x end;
end.
program p;
  import m;
begin writeln(internalSquare(9)) end.
