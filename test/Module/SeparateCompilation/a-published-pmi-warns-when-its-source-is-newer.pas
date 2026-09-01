(*
Issue #180 item 3 / issue #304: the same mtime-based staleness check as
Turbo's own .tui (see test/Turbo/Units/a-published-tui-warns-when-its-
source-is-newer.pas), on EP's .pmi side.  The companion source is only
found when it is named after the MODULE, lowercased (arith.pas for
`module Arith`) -- exactly how writePMIFiles itself names the .pmi it
publishes -- not any other name a multi-module file might have (this is
the documented, deliberate limitation: a file holding several modules,
like test/Module/SeparateCompilation's own multi.pas, has no single
matching name to look for, so this heuristic silently does not fire for
it; see warnIfInterfaceStale's own comment in Sema.cpp for why that is
the safe direction for this heuristic to fail in).

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/arith.pas -o %t.dir/arith.o
RUN: touch -d "2099-01-01 00:00:00" %t.dir/arith.pas
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/arith.o -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: warning: '{{.*}}arith.pmi' may be stale: its source '{{.*}}arith.pas' was modified more recently; recompile '{{.*}}arith.pas' to refresh it
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
