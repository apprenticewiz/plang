(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[round trip]
*)

program p;
var f: text; s: string(40);
begin
  s := 'round trip';
  rewrite(f); writeln(f, s); reset(f);
  s := 'clobbered';
  readln(f, s);
  writeln('[', s, ']')
end.
