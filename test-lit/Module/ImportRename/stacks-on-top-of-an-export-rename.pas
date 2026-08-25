(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

//--- test.pas
module m interface;
  export m = (declared => exported);
  function declared: integer;
end.
module m;
  function declared: integer; begin declared := 3 end;
end.
program p;
  import m (exported => local);
begin writeln(local()) end.
