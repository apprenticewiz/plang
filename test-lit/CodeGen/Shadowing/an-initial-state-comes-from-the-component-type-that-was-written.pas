(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:b=5
*)

program p(output);
type inner = record k: integer end;
     outer = record a: inner; b: integer end;
     po = ^outer;
var g: po;
procedure q;
type inner = record p1,p2,p3,p4,p5,p6,p7: integer;
                    p8: integer value 999 end;
begin new(g); g^.b := 5; writeln('b=', g^.b:1) end;
begin q end.
