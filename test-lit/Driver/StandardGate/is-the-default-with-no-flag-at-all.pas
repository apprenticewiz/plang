(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: Extended Pascal extension
*)

program p; var s: string(10);
begin s := 'abc'; writeln(s) end.
