(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

//--- test.pas
module m;
  var n: integer;
  procedure bump; begin n := n + 1 end;
end.
program p;
  import m;
begin n := 0; bump; writeln(n) end.
