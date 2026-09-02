(*
Issue #576: Turbo Pascal's Append(f) requires the file to already exist --
appending to a name that does not exist is InOutRes 2 ("file not found"),
not an implicit create -- confirmed against a local `fpc -Mtp` 3.2.2
install.  plang_tp_append (runtime/plang_file.cpp) used to open with C's
fopen(Name, "a"), whose "a" mode itself creates a missing file, so Append on
a nonexistent name silently succeeded and created a fresh file instead of
reporting the documented error.  This checks both halves: the IOResult
Append itself reports, and that nothing was actually created on disk (the
I-minus directive below means a subsequent Writeln/Close would silently
no-op rather than crash even if the fix regressed, so the on-disk absence is
the load-bearing half of this check, not just the IOResult).

RUN: rm -f append-on-a-nonexistent-file-sets-ioresult-2-and-creates-nothing.txt
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
RUN: not test -e append-on-a-nonexistent-file-sets-ioresult-2-and-creates-nothing.txt
*)

(*
CHECK:ioresult after append on missing file=2
*)

var
  f: text;
  r: Integer;
begin
  {$I-}
  assign(f, 'append-on-a-nonexistent-file-sets-ioresult-2-and-creates-nothing.txt');
  append(f);
  r := IOResult;
  writeln('ioresult after append on missing file=', r);
  if r = 0 then begin
    writeln(f, 'must never be written');
    close(f);
  end;
end.
