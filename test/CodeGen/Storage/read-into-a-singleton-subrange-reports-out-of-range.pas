(*
RUN: split-file %s %t.dir
RUN: %plang -frange-checks %t.dir/test.pas -o %t
RUN: not %run %t < %t.dir/stdin.txt > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of range 5..5
*)

(*
ISO §6.9.1 makes read(f, v) into `v := f^`, so §6.4.6's requirement that the
value lie within a subrange's interval applies here exactly as it does to an
assignment written out (see
singleton-subrange-assignment-reports-out-of-range.pas). BuiltinIO.cpp
mirrored CGAssign.cpp's SubLo == SubHi skip, so read(d) for a singleton
`d: 5..5` accepted 4 and left the variable holding a value its type cannot
represent.
*)

//--- test.pas
program p(output);
var d: 5..5;
begin read(d); writeln('d=', d:1) end.

//--- stdin.txt
4
