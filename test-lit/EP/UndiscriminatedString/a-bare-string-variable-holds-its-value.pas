(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[xy][zz][qq][hi]
*)

program p(output);
type r = record s: string end;
var a: string; b: array[1..2] of string; c: r;
function f: string; begin f := 'hi' end;
begin
  a := 'xy'; b[1] := 'zz'; c.s := 'qq';
  writeln('[', a, '][', b[1], '][', c.s, '][', f, ']')
end.
