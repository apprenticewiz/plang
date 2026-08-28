(*
Regression for issue #413, a regression in issue #397's own fix (PR #398).

buildPMIContent's const and type loops attempt to serialize every
interface-section declaration regardless of export-list membership -- a
pre-existing, deliberate design kept "for bound resolution", since even a
non-exported const or type may need to be written so an exported declaration
that references it can resolve.  #397's new err_pmi_cannot_serialize_export
diagnostic fired at those same sites unconditionally too, wrongly labeling an
internal-only, never-exported declaration "exported" and turning what used to
be a harmless silent drop into a hard compile failure for code that never
actually needed the dropped declaration.

Priv below is never in M's own export-list (only Foo is): canSerializeExpr
has no case for IndexExpr, so Priv's initializer (an index into a constant
imported from A) cannot be serialized -- but nothing outside M could ever
need Priv, so that should stay a silent drop, exactly as it was before #397.

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/a.pas -o %t.dir/a.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/m.pas -o %t.dir/m.o 2> %t.err
RUN: FileCheck %s --allow-empty < %t.err
RUN: test -e %t.dir/m.pmi
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/m.o %t.dir/a.o -o %t
RUN: %run %t | FileCheck --check-prefix=RESULT --strict-whitespace --match-full-lines %s
*)

(*
CHECK-NOT: cannot be represented in its module interface
*)

(*
RESULT:42
*)

//--- a.pas
module A interface;
export A = (Arr);
type ArrT = array[1..3] of integer;
const Arr = ArrT[1: 10; 2: 20; 3: 30];
end;
end.

//--- m.pas
module M interface;
import A;
export M = (Foo);
const Priv = Arr[1];
const Foo = 42;
end;
end.

//--- prog.pas
program p(output);
import M;
begin writeln(Foo) end.
