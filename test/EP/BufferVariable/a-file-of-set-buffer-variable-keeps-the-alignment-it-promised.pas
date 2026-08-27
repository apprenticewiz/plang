(*
RUN: %plang -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O1 %s -o %t.O1
RUN: %run %t.O1 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O3 %s -o %t.O3
RUN: %run %t.O3 | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
*)

(*
Issue #199.  ISO Sec6.5.5's buffer variable f^ is storage plang_file_buffer
(runtime/plang_file.cpp) allocates at run time, one component wide.  For a
`file of set of char` that component is `set of char` -- i256, which this
project's data layout ABI-aligns to 16, the same figure
a-packed-field-does-not-claim-an-alignment-it-cannot-keep.pas measured by
way of a real SIGSEGV: an under-aligned i256 store becomes a `movaps` from
-O1 on, and that faults.  Codegen loads and stores through f^ at that
alignment regardless of what backs it -- `%file.buf = call ptr
@plang_file_buffer(...)` followed by `store i256 %s, ptr %file.buf, align
16` -- so f^'s allocation has to keep the same promise a packed field could
not.  Unlike a packed field, nothing forces f^'s address to an odd offset,
so here the fix is the opposite one: keep the promise (plang_file_buffer
now asks for 16-byte-aligned memory) rather than stop making it.

A plain malloc does not documented-ly guarantee any particular alignment;
glibc's x86-64 allocator happens to hand back 16-aligned memory for every
request regardless, so this sweep does not itself reproduce a crash on
that allocator -- see the issue, which rates the finding only "medium
confidence" for exactly that reason.  It still exercises a path no earlier
test did: nothing under test/ had a `file of set of char` at all.
*)

program p(output);
var f: file of set of char; s: set of char;
begin
  rewrite(f); s := ['a'..'z']; f^ := s; put(f);
  reset(f); s := f^;
  writeln('m' in s)
end.
