(*
A file variable Assign has never touched zero-initializes Name to "" and
Mode to 0 (outside fmClosed..fmInOut -- see PascalFileLayout.h's own
comment on those constants).  This item does not build the InOutRes/runtime-
error-102 check a later item is expected to add for exactly this state; the
INTERIM behavior, documented here so that later item has a clear non-
regression baseline to change on purpose, is that plang_tp_reset reads the
empty Name the same way an explicit Assign(f, '') would and binds to the
console -- defined and non-crashing, if not yet real Turbo Pascal's own
"runtime error 102" for an unassigned file.

RUN: %plang -std=turbo %s -o %t
RUN: echo -n "never assigned, still works" | %run %t | FileCheck %s
*)

(*
CHECK:[never assigned, still works]
*)

var f: text; s: string;
begin
  reset(f);
  readln(f, s);
  writeln('[', s, ']');
end.
