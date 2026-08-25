(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
*)

program p(output);
type day = (mon, tue, wed, thu, fri, sat, sun);
var c: mon..fri; e: sat..sun;
begin c := mon; e := sat; writeln(c < e) end.
