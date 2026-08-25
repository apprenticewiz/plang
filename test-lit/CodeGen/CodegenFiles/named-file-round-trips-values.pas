(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=123
*)

program p;
var f: text; a: integer;
begin
  rewrite(f, '/tmp/plang_named_regtest.txt');
  writeln(f, 123);
  close(f);
  reset(f, '/tmp/plang_named_regtest.txt');
  read(f, a);
  close(f);
  writeln('a=', a)
end.
