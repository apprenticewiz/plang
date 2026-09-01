(*
Issue #180 item 3 / issue #304's own negative cases for the .pmi side (see
a-published-pmi-warns-when-its-source-is-newer.pas for the positive case,
and test/Turbo/Units for the .tui equivalents of both).  No warning when
the interface is up to date, and no warning at all -- not even an attempt
-- when the companion source has been removed (the ordinary shape of a
vendored/shipped module with no local .pas alongside its .pmi).

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/arith.pas -o %t.dir/arith.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/arith.o -o %t.exe1 2> %t.err1
RUN: FileCheck %s --allow-empty < %t.err1
RUN: rm %t.dir/arith.pas
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/arith.o -o %t.exe2 2> %t.err2
RUN: FileCheck %s --allow-empty < %t.err2
*)

(*
CHECK-NOT: may be stale
*)

//--- arith.pas
module Arith;
function Double(x: integer): integer;
begin Double := x * 2 end;
end.

//--- prog.pas
program p;
import Arith;
var n: integer;
begin
  n := Double(21);
  writeln(n)
end.
