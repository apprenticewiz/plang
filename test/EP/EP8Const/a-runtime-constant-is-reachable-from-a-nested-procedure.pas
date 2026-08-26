(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:outer  3.0
CHECK-NEXT:nested 3.0
*)

program p(output);
procedure outer;
const k = sqrt(4.0) + 1.0;
  procedure nested;
  begin writeln('nested ', k:3:1) end;
begin writeln('outer  ', k:3:1); nested end;
begin outer end.
