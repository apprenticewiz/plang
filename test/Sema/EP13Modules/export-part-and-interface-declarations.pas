(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M interface;
  export M = (K, Color, Count, Scale);
  const K = 3;
  type Color = (red, green);
  var Count: integer;
  function Scale(x: real; k: integer): real;
end.
module M;
  function Scale(x: real; k: integer): real;
  begin Scale := x * k end;
end.
program p;
begin end.
