(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot compare
*)

program p(output);
type day = (mon, tue, wed);
var c: mon..tue; b: 'a'..'z';
begin c := mon; b := 'q'; writeln(c = b) end.
