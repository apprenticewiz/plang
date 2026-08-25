(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:false true done
*)

program p;
var b: boolean;
begin for b := false to true do write(b, ' '); writeln('done') end.
