(*
TP's Append(f) -- unlike Rewrite, which truncates -- opens the file Assign
bound f to for writing at its current end, creating it if absent.  Real
Turbo Pascal sets Mode to fmOutput here too, the same as Rewrite (confirmed
against `fpc -Mtp`; see plang_tp_append's own comment, runtime/plang_file.cpp).

RUN: printf 'first line\n' > assign-append-appends-to-a-named-file.txt
RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: cat assign-append-appends-to-a-named-file.txt | FileCheck %s
*)

(*
CHECK:first line
CHECK-NEXT:second line
*)

var f: text;
begin
  assign(f, 'assign-append-appends-to-a-named-file.txt');
  append(f);
  writeln(f, 'second line');
  close(f);
end.
