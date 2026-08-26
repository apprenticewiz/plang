(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true /tmp/plang_result_field.txt
*)

program p;
var f: bindable text; b: BindingType;
begin
  b.name := '/tmp/plang_result_field.txt';
  bind(f, b); rewrite(f);
  writeln(binding(f).bound, ' ', binding(f).name)
end.
