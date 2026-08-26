(*
RUN: %plang -std=iso10206 -Wall %s -o %t 2> %t.err
RUN: %run %t
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: is read here before it has been given a value
*)

program p(output);
var e: string(8); r: real; c: char; i: integer;
begin e := '0.0-4'; readstr(e, r, c, i);
  writeln(r:3:1, ' ', c, ' ', i:1) end.
