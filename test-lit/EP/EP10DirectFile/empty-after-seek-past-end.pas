(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:past-end
CHECK-NEXT:at-component
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 42; write(f, v);
  { file now has 1 component at index 0; position = 1 (past end) }
  if empty(f) then writeln('past-end') else writeln('not-past-end');
  seekread(f, 0);
  if empty(f) then writeln('past-end') else writeln('at-component')
end.
