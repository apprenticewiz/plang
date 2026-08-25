(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[a string schema] len=15
*)

program p(output); type ps = ^string; var q: ps;
begin new(q, 20); q^ := 'a string schema';
      writeln('[', q^, '] len=', length(q^):1); dispose(q) end.
