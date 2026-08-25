(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not compatible
*)

program p(output);
type bad = array [1..8] of char value '*';
var c: bad;
begin writeln(c[1]) end.
