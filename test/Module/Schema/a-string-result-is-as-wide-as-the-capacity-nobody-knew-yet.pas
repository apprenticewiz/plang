(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:concat 400
CHECK-NEXT:substr 400
CHECK-NEXT:trim   400
*)

program p(output);
var q: ^string; s: string(400); i: integer;
begin new(q, 400); q^ := '';
      for i := 1 to 400 do q^ := q^ + 'y';
      writeln('concat ', length(q^):1);
      s := substr(q^, 1, 400); writeln('substr ', length(s):1);
      s := trim(q^);           writeln('trim   ', length(s):1) end.
