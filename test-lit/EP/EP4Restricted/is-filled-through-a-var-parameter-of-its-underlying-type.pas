(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

module m interface;
export m = (handle, seth, showh);
type rep = record n: integer end;
     handle = restricted rep;
procedure seth(var h: rep; v: integer);
procedure showh(h: rep);
end;
procedure seth; begin h.n := v end;
procedure showh; begin writeln(h.n) end;
end.
program p(output);
import m;
var x: handle;
begin seth(x, 7); showh(x) end.
