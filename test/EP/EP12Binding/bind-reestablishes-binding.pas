(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bound
*)

program p;
var f: bindable text; b, b2: BindingType;
begin
  rewrite(f);
  unbind(f);
  b.name := '/tmp/plang_bind_reestablish.txt';
  bind(f, b);
  rewrite(f);
  b2 := binding(f);
  if b2.bound then writeln('bound') else writeln('unbound')
end.
