(*
buildPMIContent (lib/Frontend/Frontend.cpp) turns each declaration in a
module's interface part back into Pascal text for the .pmi another compile
later imports.  canSerializeExpr has no case for IndexExpr, so a const whose
initializer indexes into a constant imported from another module used to be
left out of the .pmi with nothing said -- even though M's own export-list
named C.  m.pmi came out with no "const C" line at all, compiling m.pas
reported success, and only a later, unrelated "import M; writeln(C)" failed,
with a plain "undefined identifier 'C'" nowhere near the real cause (#397).

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/a.pas -o %t.dir/a.o
RUN: not %plang -std=iso10206 -I%t.dir -c %t.dir/m.pas -o %t.dir/m.o 2> %t.err
RUN: FileCheck %s < %t.err
RUN: test ! -e %t.dir/m.pmi
*)

(*
CHECK: exported constant 'C' of module 'M' cannot be represented in its module interface
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
export M = (C);
const C = Arr[1];
end;
end.
