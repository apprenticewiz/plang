(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[xy][xy]
*)

program p;
var a, b: string(8) value 'xy';
begin writeln('[', a, '][', b, ']') end.
