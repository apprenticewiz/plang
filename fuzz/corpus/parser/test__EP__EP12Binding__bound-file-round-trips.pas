(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:through the binding
*)

program p;
var f: bindable text; b: BindingType; s: string(40);
begin
  b.name := '/tmp/plang_bind_roundtrip.txt';
  bind(f, b);
  rewrite(f); writeln(f, 'through the binding'); close(f);
  reset(f); readln(f, s); close(f);
  writeln(s)
end.
