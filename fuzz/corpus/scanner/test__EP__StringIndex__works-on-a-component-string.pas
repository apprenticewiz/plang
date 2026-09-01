(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:e
*)

program p(output); var r: record s: string(10) end;
begin r.s := 'hey'; writeln(r.s[2]) end.
