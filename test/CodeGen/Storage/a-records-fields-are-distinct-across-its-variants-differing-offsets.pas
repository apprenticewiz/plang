(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: duplicate field name 'x'
ERR-ABSENT-NOT: LLVM ERROR
*)

(*
ISO Sec6.4.3.3: a record's field identifiers are distinct, across the fixed
part and EVERY variant alike -- a variant selects which fields exist, not
which of two same-named fields is meant.  The check existed for the fixed
part and not for the variant part, seventy lines apart in one file.

A repeat was silently skipped, keeping the first declaration and leaving
the second unreachable.  Where the two alternatives put the field at
DIFFERENT offsets, Sema's flattened field list and codegen's
per-alternative layout disagreed, and the offset gate aborted the
compiler with no file and no line -- a user's mistake reported as an
internal error, which is still the wrong answer: it must be rejected as a
plain diagnostic instead.
*)

program p(output);
type r = record
  h: integer;
  case b: boolean of
    true:  (p: integer; x: integer);
    false: (x: real; q: integer)
end;
var v: r;
begin v.h := 1; writeln(v.h:1) end.
