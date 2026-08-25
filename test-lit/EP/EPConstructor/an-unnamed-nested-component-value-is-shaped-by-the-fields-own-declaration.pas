(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 30
*)

program p(output);
type comp = array[1..3] of integer;
     rec = record f: comp end;
procedure outer;
type comp = record z: integer end;
var r: rec value [f: [1:10; 2:20; 3:30]];
begin writeln(r.f[1]:1, ' ', r.f[2]:1, ' ', r.f[3]:1) end;
begin outer end.
