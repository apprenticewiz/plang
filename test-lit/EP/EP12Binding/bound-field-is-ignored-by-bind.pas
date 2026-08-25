(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:unbound
*)

program p;
var f: bindable text; b, b2: BindingType;
begin
  rewrite(f);
  b.bound := true;
  bind(f, b);
  b2 := binding(f);
  if b2.bound then writeln('bound') else writeln('unbound')
end.
