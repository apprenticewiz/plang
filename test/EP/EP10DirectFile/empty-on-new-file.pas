(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:empty
*)

program p;
var f: file of integer;
begin
  rewrite(f);
  if empty(f) then writeln('empty') else writeln('not-empty')
end.
