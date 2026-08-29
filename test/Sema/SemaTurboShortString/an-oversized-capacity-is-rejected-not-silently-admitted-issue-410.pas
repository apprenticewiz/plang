(*
issue #410's own class of bug, reopened by a new type unless guarded: Sema's
var/local/parameter/return-type declaration size gates all read
Sema::byteSizeOf through `Sz && *Sz > Limit`, so a missing `case
TypeKind::ShortString:` there (falling into the shared `default: return
std::nullopt` a handful of genuinely-unsized kinds like ConformantArray/
Schema legitimately use) would make `Sz &&` false and silently ADMIT a
`string[huge N]` the 1 GiB gate exists to refuse -- exactly the same
unbounded-declaration DoS #410 fixed once already, for a different type.
SemaType.cpp's byteSizeOf/byteAlignOf ShortString cases exist to give this a
real, honest byte count instead, so the existing gate sees it and rejects
it, the same way it rejects every other oversized declaration (see
test/Sema/SemaClean/global-array-extent-arithmetic-overflow-is-still-too-
large.pas for the array-typed sibling of this same gate).

RUN: not %plang -dump-ast -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var s: string[2000000000];
begin
  writeln('ok')
end.

(*
CHECK: 's' is too large to be a global variable (2000000001 bytes; the limit is 1073741824 bytes) -- declare it on the heap (New/GetMem) instead
*)
