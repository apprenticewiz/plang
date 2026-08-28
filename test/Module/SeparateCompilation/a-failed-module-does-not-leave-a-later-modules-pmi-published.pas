(*
Regression for issue #414, a regression in issue #397's own fix (PR #398).

writePMIFiles' per-module loop merely "continue"d to the next module when a
serialization failure was detected through Diags (#397's mechanism), unlike
the I/O-failure paths a few lines below, which return false immediately.  So
in this one compilation unit (module A, a module Bad whose exported constant
canSerializeExpr cannot represent, module Good with no issues, and a program
importing both), Good's .pmi -- coming after Bad's in the loop -- still got
published even though the whole compile fails and no .o is produced,
breaking the atomic/all-or-nothing publish contract issues #168/#173/#175
established for .pmi writing: a stale-but-valid-looking interface file was
left for a later, unrelated compile to trust.

Expected: the compile fails (Bad's Foo really is in its own export-list, so
that diagnostic is correct and must still fire), and none of this
compilation unit's .pmi files -- not even ones from modules that serialized
fine, whether before or after the failing one -- are left on disk.

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: not %plang -std=iso10206 -I%t.dir -c %t.dir/multi.pas -o %t.dir/multi.o 2> %t.err
RUN: FileCheck %s < %t.err
RUN: test ! -e %t.dir/multi.o
RUN: test ! -e %t.dir/a.pmi
RUN: test ! -e %t.dir/bad.pmi
RUN: test ! -e %t.dir/good.pmi
*)

(*
CHECK: exported constant 'Foo' of module 'Bad' cannot be represented in its module interface
*)

//--- multi.pas
module A interface;
export A = (Arr);
type ArrT = array[1..3] of integer;
const Arr = ArrT[1: 10; 2: 20; 3: 30];
end;
end.

module Bad interface;
import A;
export Bad = (Foo);
const Foo = Arr[1];
end;
end.

module Good interface;
export Good = (Ok);
const Ok = 1;
end;
end.

program Multi;
import Bad; Good;
begin writeln(Ok) end.
