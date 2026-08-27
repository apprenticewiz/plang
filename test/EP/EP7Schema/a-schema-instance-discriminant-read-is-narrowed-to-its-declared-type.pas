(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:>a
*)

program p;
type t(c: char) = record k: integer end;
var x: t('a');
begin
  writeln('>' + x.c)
end.
