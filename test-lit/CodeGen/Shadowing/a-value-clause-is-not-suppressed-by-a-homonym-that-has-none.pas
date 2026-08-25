(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:outer 7
CHECK-NEXT:inner 7
*)

program p(output);
type base = integer value 7;
     u = base;
var g: u;
procedure inner;
type base = integer;
var l: u;
begin writeln('inner ', l:1) end;
begin writeln('outer ', g:1); inner end.
