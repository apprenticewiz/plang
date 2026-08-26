(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot assign 'inner'
*)

program p(output);
type inner = record a,b,c,d,e,f,g,h: integer end;
     outer = record n: integer; m: integer end;
var o: outer; iv: inner;
begin iv.a := 1; o := outer[n: iv; m: 9]; writeln(o.n:1) end.
