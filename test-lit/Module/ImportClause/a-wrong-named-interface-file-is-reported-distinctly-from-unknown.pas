(*
A syntactically-valid .pmi that declares a DIFFERENT module than the one
being imported -- distinguishes "found a file, but it isn't the module
you asked for" from "no file by that name at all".

RUN: split-file %s %t.dir
RUN: not %plang -std=iso10206 -I%t.dir %t.dir/prog.pas -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: does not declare
ERR-ABSENT-NOT: no module named
*)

//--- broken.pmi
module notthemodule interface;
end.

//--- prog.pas
program p;
  import broken;
begin end.
