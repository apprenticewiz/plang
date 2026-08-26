(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:before
*)

program p;
begin writeln('before'); halt(3); writeln('after') end.
