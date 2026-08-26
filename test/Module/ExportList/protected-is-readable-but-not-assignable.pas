(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

//--- test.pas
module m interface;
  export m = (protected count, bump);
  var count: integer;
  procedure bump;
end.
module m;
  var count: integer;
  procedure bump; begin count := count + 1 end;
  to begin do count := 0;
end.
program p;
  import m;
begin bump; bump; writeln(count) end.
