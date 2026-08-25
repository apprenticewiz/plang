(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'writeln'
*)

program p(output);
var f: file of integer; x: integer;
begin rewrite(f); writeln(f) end.
