(*
Regression gate: Tier 3 Cluster C item 6's whole 13-builtin family
(FilePos/FileSize/SeekEof/SeekEoln/Seek/Truncate/BlockRead/BlockWrite/
Erase/Rename/Flush/SetTextBuf) is registered in Builtins.def with
Dialects=TP only -- err_turbo_required_name's wording, "is a Turbo Pascal
extension and is only available under -std=turbo", the same message every
other Turbo-only required identifier already gets (see e.g.
ioresult-and-inoutres-are-turbo-only-*.pas, this file's own direct model).
A representative sample -- one Group A function (FilePos), one Group B
statement with a fixed-shape record-transfer arm (BlockRead), and one
without one (Erase) -- rather than all thirteen, the same "confirm the
mechanism, not every name" precedent that file's own comment sets.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'filepos' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'blockread' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'erase' is a Turbo Pascal extension and is only available under -std=turbo
*)

program p;
var
  f: file of Integer;
  buf: array[0..9] of Integer;
  n: Integer;
begin
  n := filepos(f);
  blockread(f, buf, 1);
  erase(f)
end.
