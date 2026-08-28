(*
A Turbo identifier label is a Pascal identifier, and Pascal identifiers are
case-insensitive: `label Done;` and `goto DONE` name the same label.  This is
not automatic just because SymbolTable::lookup lowercases its own key --
Sema's CurrentBlockLabels (which decides whether a goto stays inside its own
block or leaves it) and CodeGen's LabelGotoEngine both key on the label's
spelling with plain, case-sensitive string equality, so the spelling itself
has to already agree by the time it reaches either one.  canonicalLabel
(ParserInternal.h) is where that happens, for every one of a label
declaration, a goto target and a labelled statement alike.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before
CHECK-NEXT:reached
*)

program p(output);
label Done;
begin
  writeln('before');
  goto DONE;
  writeln('skipped');
dONE:
  writeln('reached')
end.
