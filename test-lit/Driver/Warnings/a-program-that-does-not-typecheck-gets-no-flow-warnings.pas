(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: undefined identifier
ERR-ABSENT-NOT: before it has been given
*)

program p(output);
var i: integer;
begin writeln(i); writeln(nosuchname) end.
