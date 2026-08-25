(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

//--- test.pas
module m interface;
  export m = (visible);
  function visible: integer;
end.
module m;
  var hidden: integer;
  function visible: integer; begin visible := 7 end;
  procedure secret; begin hidden := 1 end;
end.
program p;
  import m;
begin writeln(visible()) end.
