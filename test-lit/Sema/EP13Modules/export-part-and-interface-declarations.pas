(*
RUN: %plang_ep -dump-ast %s
*)

module M interface;
  export M = (K, Color, Count, Scale);
  const K = 3;
  type Color = (red, green);
  var Count: integer;
  function Scale(x: real; k: integer): real;
end.
program p;
begin end.
