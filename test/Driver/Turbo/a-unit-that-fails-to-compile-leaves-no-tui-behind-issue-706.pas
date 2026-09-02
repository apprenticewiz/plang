(*
Issue #706: writeTUIFile publishes a unit's own .tui BEFORE Codegen::emitUnit
runs (Frontend.cpp's compileRequest), and until this fix, nothing removed it
again if emitUnit then failed -- the .pmi sibling mechanism (writePMIFiles)
has exactly this all-or-nothing cleanup already, for issue #414. This test
covers the foundational half of that "publish only on a fully successful
compile" contract that IS reliably reproducible with ordinary Pascal: a unit
whose SEMA check fails must never have a .tui appear at all, since
compileRequest bails out (returning false) before ever calling writeTUIFile
in the first place. (The other half -- a unit that passes Sema but whose
Codegen::emitUnit itself then fails -- is exercised by the SAME cleanup code
this fix added, immediately after writeTUIFile's call in compileRequest;
Sema's own hasErrors()-driven UnitOk check is comprehensive enough that no
ordinary Pascal program currently reaches Codegen with something it rejects,
so that branch is covered by code inspection/symmetry with writePMIFiles'
own cleanupPublished, not by a live repro here.)

RUN: split-file %s %t.dir
RUN: not %plang -std=turbo -c %t.dir/badunit.pas -o %t.dir/badunit.o 2> %t.err
RUN: FileCheck %s < %t.err
RUN: test ! -e %t.dir/badunit.o
RUN: test ! -e %t.dir/badunit.tui
*)

(*
CHECK: undefined identifier 'UndeclaredIdentifierZZZ'
*)

//--- badunit.pas
unit BadUnit;

interface

procedure Foo;

implementation

procedure Foo;
begin
  UndeclaredIdentifierZZZ := 1;
end;

end.
