(*
The read side of Turbo's Assign/Reset round trip -- see
assign-rewrite-writes-a-named-file.pas, right next to this test, for the
write side.  The fixture file is written directly by the RUN line, not by
this program, so this test exercises Reset alone: Assign(f, name) binds f,
and Reset(f) -- unlike ISO/EP's reset(f, name) -- opens whatever name that
was with no filename argument of its own.

RUN: printf 'hello there\n' > assign-reset-reads-a-named-file.txt
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:[hello there]
*)

var f: text; s: string;
begin
  assign(f, 'assign-reset-reads-a-named-file.txt');
  reset(f);
  readln(f, s);
  writeln('[', s, ']');
  close(f);
end.
