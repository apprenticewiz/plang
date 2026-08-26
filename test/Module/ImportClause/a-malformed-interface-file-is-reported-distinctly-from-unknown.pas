(*
A hand-authored, non-Pascal .pmi sitting where the search path expects a
real one -- distinguishes "found a file but could not parse it" from
"no file by that name at all", two different diagnostics.

RUN: split-file %s %t.dir
RUN: not %plang -std=iso10206 -I%t.dir %t.dir/prog.pas -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: could not be parsed
ERR-ABSENT-NOT: no module named
*)

//--- broken.pmi
not valid pascal ???

//--- prog.pas
program p;
  import broken;
begin end.
