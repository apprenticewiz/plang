(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi]
*)

program p(output); type ps = ^string(10); var q: ps;
begin new(q); q^ := 'hi'; writeln('[', q^, ']'); dispose(q) end.
