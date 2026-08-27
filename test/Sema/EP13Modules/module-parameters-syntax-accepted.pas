(*
RUN: split-file %s %t.dir
RUN: %plang_ep -dump-ast %t.dir/test.pas
*)

//--- test.pas
module IO(input, output) interface;
  export function readline(var s: string(80)): integer;
end.
module IO(input, output);
  function readline(var s: string(80)): integer;
  begin readline := 0 end;
end.
program p;
begin end.
