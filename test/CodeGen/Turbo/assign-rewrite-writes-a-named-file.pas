(*
Turbo's own file model (real Borland/FPC's Assign/Reset/Rewrite/Append):
Assign(f, name) binds f to an external filename, and Rewrite(f) -- unlike
ISO/EP's rewrite(f, name) -- takes no filename argument of its own; it opens
whatever Assign bound f to.  This checks the write side of that round trip
by reading the raw file back with `cat` after the program exits.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: cat assign-rewrite-writes-a-named-file.txt | FileCheck %s
*)

(*
CHECK:hello from assign and rewrite
*)

var f: text;
begin
  assign(f, 'assign-rewrite-writes-a-named-file.txt');
  rewrite(f);
  writeln(f, 'hello from assign and rewrite');
  close(f);
end.
