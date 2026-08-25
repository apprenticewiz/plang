(*
RUN: split-file %s %t.dir
RUN: %plang -frange-checks %t.dir/test.pas -o %t
RUN: not %run %t < %t.dir/stdin.txt > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of range 1..9
*)

(*
ISO Sec6.9.1 makes read(f, v) into `v := f^`, so Sec6.4.6's requirement that
the value lie within a subrange's interval applies exactly as it does to an
assignment written out. Nothing checked it, so read(d) for d: 1..9 accepted
99 and left the variable holding a value its type cannot represent -- which
everything downstream then trusts: an array indexed by it, a case selector,
a for bound. This is the out-of-range case: 99 must be rejected.
*)

//--- test.pas
program p(output);
var d: 1..9;
begin read(d); writeln('d=', d:1) end.

//--- stdin.txt
99
