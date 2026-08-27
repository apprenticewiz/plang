(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module MathUtil interface;
  export function Add(a: integer; b: integer): integer;
end.
module MathUtil;
  function Add(a: integer; b: integer): integer;
  begin Add := a + b end;
end.
program p;
begin end.
