(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: used here before its declaration
*)

program p(output);
type t = record f: u end;
     u = integer;
var v: t;
begin writeln('unreachable') end.
