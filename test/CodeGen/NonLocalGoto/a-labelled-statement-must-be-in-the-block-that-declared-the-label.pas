(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: enclosing block
ERR-ABSENT-NOT: internal error
*)

program p(output);
label 1;
procedure q;
begin
1: writeln('inner')
end;
begin q end.
