(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:birth=0
CHECK-NEXT:[first]
CHECK-NEXT:[a much longer second]
*)

program p(output); type ps = ^string; var q: ps;
begin new(q, 20);
      writeln('birth=', length(q^):1);
      q^ := 'first';  writeln('[', q^, ']');
      q^ := 'a much longer second'; writeln('[', q^, ']');
      dispose(q) end.
