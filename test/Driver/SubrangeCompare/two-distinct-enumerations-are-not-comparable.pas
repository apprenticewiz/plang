(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot compare
*)

program p(output);
type day = (mon, tue, wed);
     color = (red, green, blue);
var d: day; k: color;
begin d := mon; k := red; writeln(d = k) end.
