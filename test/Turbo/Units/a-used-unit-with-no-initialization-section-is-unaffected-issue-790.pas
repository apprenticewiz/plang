(*
Issue #790: a unit that never writes a `begin ... end` initialization
section at all (implementation runs straight into `end.`) must be entirely
unaffected by this feature -- no spurious output, no crash, and its
exported variable/procedure keep working exactly as before.  Confirms
CodeGen still safely calls `__plang_init_<name>` for such a unit (the
function always exists -- see emitUnitInitFn's own header comment -- it
just has nothing to do), rather than this feature depending on every used
unit having real init code.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/noinitunit.pas -o %t.dir/noinitunit.o
RUN: rm %t.dir/noinitunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/noinitunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before=0
CHECK-NEXT:after=5
*)

//--- noinitunit.pas
unit NoInitUnit;

interface

var NIVal: Integer;
procedure SetIt;

implementation

procedure SetIt;
begin
  NIVal := 5;
end;

end.

//--- main.pas
program NoInitProg;
uses NoInitUnit;
begin
  Writeln('before=', NIVal);
  SetIt;
  Writeln('after=', NIVal);
end.
