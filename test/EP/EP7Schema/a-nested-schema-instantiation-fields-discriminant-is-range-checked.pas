(*
Issue #409.  emitNewSchema's discriminant range-check loop (added by issue
#230) only ran for the literal argument list passed directly to `new()`
itself.  It never reached a nested schema-instantiation FIELD inside the
object being allocated whose own discriminant is derived from the
enclosing schema's discriminant: `outer(n) = record x: inner(n); k: integer
end` binds inner's formal m (declared `small = 1..3`) to outer's own n, and
an out-of-range n went completely unchecked -- neither a compile-time nor a
run-time diagnostic -- even though q^.x is never touched below at all; only
q^.k is.  The trap has to fire from new() itself, exactly as `new(q, 500)`
directly on a `^inner` already does (this file's own
new-rejects-a-discriminant-outside-its-declared-subrange.pas), whether or
not the program later reads the field back.

Sibling to #393 (round 7)'s sizing fix and #408's discriminant-read fix for
this exact shape; this is the discriminant RANGE-CHECK sibling.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 500 out of range 1..3
*)

program p(output);
type small = 1..3;
     inner(m: small) = array[1..m] of integer;
     outer(n: integer) = record x: inner(n); k: integer end;
var q: ^outer; i: integer;
begin
  i := 500;
  new(q, i);
  q^.k := 99;
  writeln('k=', q^.k:1);
  writeln('done')
end.
