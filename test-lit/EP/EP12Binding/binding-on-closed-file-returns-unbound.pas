(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:unbound
*)

program p;
var f: bindable text; b: BindingType;
begin
  b := binding(f);
  if b.bound then writeln('bound') else writeln('unbound')
end.
