(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not a usable extent
*)

program p(output);
type ps = ^string;
var q: ps; n: integer;
begin n := -5; writeln('before'); new(q, n); writeln('after') end.
