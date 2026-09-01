(*
RUN: %plang -ferror-limit=1 %s -o %t 2> %t.err; true
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: unrecognized argument
*)

program p;
begin a:=1; b:=2; c:=3 end.
