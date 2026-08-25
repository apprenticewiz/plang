(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:600
CHECK-NEXT:10
*)

program p(output);
procedure a;
var n: integer;
  procedure c;
  var n: integer;
    procedure d; begin n := n + 100 end;
  begin n := 500; d; writeln(n) end;
begin n := 10; c; writeln(n) end;
begin a end.
