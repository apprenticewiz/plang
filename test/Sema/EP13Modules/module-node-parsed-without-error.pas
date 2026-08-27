(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module M interface;
  export function Scale(x: real; k: integer): real;
end.
module M;
  function Scale(x: real; k: integer): real;
  begin Scale := x * k end;
end.
program p;
begin end.
