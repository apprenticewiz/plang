(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:c 600
CHECK-NEXT:a 13
*)

program p(output);
procedure a;
var n: integer;
  procedure b; begin n := n + 1 end;
  procedure c;
  var n: integer;
    procedure d; begin n := n + 100 end;
  begin n := 500; b; d; writeln('c ', n) end;
  procedure e; begin b end;
begin n := 10; b; c; e; writeln('a ', n) end;
begin a end.
