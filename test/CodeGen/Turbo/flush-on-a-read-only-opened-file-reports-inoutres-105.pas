(*
Issue #664's other half: Flush(f) on a read-only-opened file traps with
InOutRes 105 ("file not open for output"), confirmed against `fpc -Mtp` --
even though fflush(3) itself is a harmless no-op on a read-only C stream
(glibc just discards any buffered input there, never sets ferror()), so
this cannot be detected the way every write path elsewhere in this file
detects a direction violation and needed its own check of F->Readable.
FileMode is forced to 0 before Reset for the same reason the existing
BlockWrite direction tests already force it (Tier 3's own gap fix has Reset
honor FileMode's read-write default of 2, so without this the file would
be reopened read-write and Flush would genuinely succeed).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:flush-on-readonly ioresult=105
*)

var
  f: Text;
begin
  assign(f, 'flush-on-a-read-only-opened-file-reports-inoutres-105.txt');
  rewrite(f);
  write(f, 'abc');
  close(f);

  FileMode := 0;
  reset(f); (* read-only *)
  {$I-}
  flush(f);
  writeln('flush-on-readonly ioresult=', IOResult);
  {$I+}
  close(f);
end.
