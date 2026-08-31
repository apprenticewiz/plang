(*
Turbo Tier 4, Cluster C item 7's shipped `Printer` unit: Lst starts out
already open (see the sibling
the-shipped-printer-unit-auto-binds-lst-to-a-writable-text-file.pas test),
but a caller can still Assign/Rewrite it again to redirect output elsewhere
entirely -- ordinary, already-existing Tier 3 file-model behavior
(reassigning any already-open file variable), not anything this unit needs
to add of its own; see share/plang/units/Printer.pas' own header comment.
This is also the shape a caller who actually wants a live `lpr` pipe would
use (`Assign(Lst, '|lpr'); Rewrite(Lst);`), just pointed at a plain file
here to stay CI-safe.

RUN: rm -f %t.redirected
RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: env PLANG_LST_PATH=%t.unused %run %t %t.redirected
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.redirected
*)

(*
CHECK:redirected output
*)

program RedirectLst;
uses Printer;
var
  Dest: string;
begin
  Dest := ParamStr(1);
  Assign(Lst, Dest);
  Rewrite(Lst);
  Writeln(Lst, 'redirected output');
  Close(Lst);
end.
