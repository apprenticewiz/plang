(*
Issue #694: Pascal identifiers are case-insensitive, so Sema resolves a call
site that spells a unit's export differently from how the unit itself
declared it (getdate vs GetDate) just fine -- but the mangled symbol name
CodeGen builds for the CALL has to be the exact same string the DEFINING
unit's own compile emitted, or the two disagree and the link fails.
importLinkName (CGLinkage.cpp) fell back to the call site's own spelling
whenever no LinkName had been recorded for the import, and nothing recorded
one for an ordinary (non-renamed) unit export -- checkUnitInterfaceOnly
(Sema.cpp) now stamps each exported Symbol's LinkName with the spelling the
INTERFACE itself declared, at export-harvest time, so every caller mangles
to the same symbol regardless of how it spells the call.  Exercises both a
procedure and a variable, called/referenced with a spelling that differs
from the declaring unit's own in case only.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/spelling.pas -o %t.dir/spelling.o
RUN: rm %t.dir/spelling.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/prog.pas %t.dir/spelling.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
CHECK-NEXT:42
*)

//--- spelling.pas
unit Spelling;

interface

var
  TheAnswer: Integer;

procedure GetDate(var X: Integer);

implementation

procedure GetDate(var X: Integer);
begin
  X := 99;
end;

end.

//--- prog.pas
program TestCase;

uses Spelling;

var
  Y: Integer;

begin
  getdate(Y);           { call-site spelling differs from the unit's own }
  writeln(Y);
  theanswer := 42;       { var reference spelling differs too }
  writeln(theanswer);
end.
