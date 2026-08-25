(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:named
*)

program p;
type bf = bindable text;
var f: bf; b: BindingType; s: string(30);
begin
  b.name := '/tmp/plang_bind_namedtype.txt';
  bind(f, b);
  rewrite(f); writeln(f, 'named'); close(f);
  reset(f); readln(f, s); close(f);
  writeln(s)
end.
