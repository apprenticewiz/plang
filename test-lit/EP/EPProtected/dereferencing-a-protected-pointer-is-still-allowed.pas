(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
type ip = ^integer;
var q: ip;
procedure setit(protected r: ip); begin r^ := 42 end;
begin new(q); q^ := 1; setit(q); writeln(q^:1) end.
