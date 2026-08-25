(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:twenty chars exactly 999
*)

program p(output);
const n = 20;
type r = record s: string(n); tail: integer end;
procedure inner;
const n = 3;
var l: r;
begin l.s := 'abc' end;
procedure later;
var m: r;
begin
  m.tail := 999;
  m.s := 'twenty chars exactly';
  writeln(m.s, ' ', m.tail:1)
end;
begin inner; later end.
