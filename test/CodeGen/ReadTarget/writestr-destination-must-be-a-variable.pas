(*
Issue #412: writestr's destination argument (e in writestr(e, ...)) was
never checked for assignability, unlike read/readln's targets (#224).  A
non-variable destination fell through to the generic "evaluate for side
effects only" fallback and reached codegen with no address to format into,
aborting the compiler instead of being refused at the call site.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: writestr destination must be a variable
*)

program t;
begin writestr('x', 42) end.
