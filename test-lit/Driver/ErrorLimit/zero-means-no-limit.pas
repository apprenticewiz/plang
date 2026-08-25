(*
-ferror-limit=0 reports the identical count an unlimited compile does
(8, empirically confirmed against this exact program) -- explicit zero is
"no limit", not "limit of zero".

RUN: not %plang %s -o %t 2> %t.unlimited.err
RUN: grep -c 'error: ' %t.unlimited.err | FileCheck --check-prefix=UNLIMITED --strict-whitespace --match-full-lines %s
RUN: not %plang -ferror-limit=0 %s -o %t 2> %t.explicit.err
RUN: grep -c 'error: ' %t.explicit.err | FileCheck --check-prefix=EXPLICIT --strict-whitespace --match-full-lines %s
*)

(*
UNLIMITED:8
EXPLICIT:8
*)

program p;
begin a:=1; b:=2; c:=3; d:=4 end.
