(*
Indexing a plain array with a non-ordinal expression is one root cause,
not two: checkIndex used to emit err_index_not_ordinal and then, without
returning, fall through into the assignment-compatibility check, which
fired err_index_type_mismatch for the very same index expression at the
very same location.  Only the first diagnostic should survive (issue
#128).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: array index must be ordinal
CHECK-NOT: index type mismatch
COUNT:1
*)

program p;
var
  arr: array[1..3] of integer;
  idx: real;
begin
  arr[idx] := 1
end.
