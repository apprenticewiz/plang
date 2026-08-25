(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: contains itself
ERR-ABSENT-NOT: crashed
*)

program p(output);
type t(n: integer) = record next: t(n); k: integer end;
var v: t(4);
begin v.k := 7; writeln(v.k:1) end.
