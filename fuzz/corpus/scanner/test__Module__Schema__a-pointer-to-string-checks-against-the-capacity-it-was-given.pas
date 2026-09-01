(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: string(4)
*)

program p(output); type ps = ^string; var q: ps;
begin new(q, 4); q^ := 'far too long' end.
