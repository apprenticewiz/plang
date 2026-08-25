(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:20
*)

//--- test.pas
module M;
  const k = 10;
  type v = array[1..3] of integer;
  var count: integer;
  procedure bump; begin count := count + k end;
  to begin do count := 0;
end.
program p;
  import M;
var x: v;
begin bump; bump; x[1] := count; writeln(x[1]) end.
