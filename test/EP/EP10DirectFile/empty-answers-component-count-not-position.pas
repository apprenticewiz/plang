(*
Issue #686: ISO 10206 §6.7.6.5/D.68 defines empty(f) as "f has no
components", a question about the file's CONTENT, not about where f is
currently positioned.  A 1-component file read to end-of-file (position
past its only component) is still not empty -- it has one component, just
none left to read from here.  plang used to answer this by comparing the
saved position against the end of file (`saved >= end`), which made a
freshly-written or freshly-read 1-component file register as "empty"
merely for being positioned at EOF.

RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:not-empty
CHECK-NEXT:not-empty
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 42; write(f, v);
  { file now has 1 component at index 0; position = 1 (past end) }
  if empty(f) then writeln('empty') else writeln('not-empty');
  seekread(f, 0);
  { same 1 component, now positioned AT it rather than past it }
  if empty(f) then writeln('empty') else writeln('not-empty')
end.
