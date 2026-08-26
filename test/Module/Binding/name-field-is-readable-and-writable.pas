(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[/tmp/plang_bind_namefield.txt]
*)

program p;
var f: bindable text; b: BindingType;
begin
  b.name := '/tmp/plang_bind_namefield.txt';
  bind(f, b);
  b := binding(f);
  writeln('[', b.name, ']')
end.
