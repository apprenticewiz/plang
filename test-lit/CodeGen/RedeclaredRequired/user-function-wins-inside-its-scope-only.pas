(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:999
*)

program p(output);
procedure outer;
begin writeln(abs(-7)) end;
procedure inner;
  function abs(x: integer): integer;
  begin abs := 999 end;
begin writeln(abs(-7)) end;
begin outer; inner end.
