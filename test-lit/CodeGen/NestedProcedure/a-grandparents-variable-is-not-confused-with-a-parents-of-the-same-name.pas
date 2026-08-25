(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
procedure b;
var n: integer;
  procedure d; begin writeln(n) end;
  procedure e;
  var n: integer;
    procedure f; begin d end;
  begin n := 100; f end;
begin n := 42; e end;
begin b end.
