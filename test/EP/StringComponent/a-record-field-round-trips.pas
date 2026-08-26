(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi][hi!] true
*)

program p(output); var r: record s: string(10) end; n: string(20);
begin r.s := 'hi'; n := r.s + '!';
 writeln('[', r.s, '][', n, ']', ' ', r.s = 'hi') end.
