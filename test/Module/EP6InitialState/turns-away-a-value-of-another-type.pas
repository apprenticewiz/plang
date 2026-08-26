(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not compatible
*)

program p(output);
type bad = integer value 'x';
var b: bad;
begin writeln(b) end.
