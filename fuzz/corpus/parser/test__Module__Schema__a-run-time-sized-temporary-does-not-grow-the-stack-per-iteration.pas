(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 abcd
*)

program p(output);
var q: ^string; i: integer;
begin new(q, 4000);
      for i := 1 to 2000000 do begin q^ := 'abc'; q^ := q^ + 'd' end;
      writeln(length(q^):1, ' ', q^) end.
