(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bound
CHECK-NEXT:unbound
*)

program p;
var b: BindingType;
begin
  b.bound := true;
  if b.bound then writeln('bound') else writeln('unbound');
  b.bound := false;
  if b.bound then writeln('bound') else writeln('unbound')
end.
