(*
ISO Sec6.6.1: the forward directive promises "a defining-occurrence of the
procedure-identifier or function-identifier ... in the same block" -- a
later heading, with a body, that completes it.  checkProcSignature clears
IsForward the moment a matching heading comes along, but nothing audited
the case where none ever does: this used to compile clean and fail only at
link time against the mangled name ("undefined symbol pas_neverdef"),
rather than being caught here against the source identifier (issue #266).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 'neverdef' is declared 'forward' but is never given a defining declaration
CHECK-NOT: undefined symbol
COUNT:1
*)

program p;
procedure neverdef(x: integer); forward;
begin
  neverdef(1)
end.
