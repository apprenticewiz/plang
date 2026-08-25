(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:via parameter
*)

program p;
var f: bindable text; s: string(30);
procedure attach(var g: bindable text; path: string(60));
var b: BindingType;
begin b.name := path; bind(g, b) end;
begin
  attach(f, '/tmp/plang_bind_varparam.txt');
  rewrite(f); writeln(f, 'via parameter'); close(f);
  reset(f); readln(f, s); close(f);
  writeln(s)
end.
