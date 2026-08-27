(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: duplicate field name 'a'
ERR-ABSENT-NOT: LLVM ERROR
*)

(*
ISO Sec6.4.3.3: a record's field identifiers are distinct, and the variant
part's own tag field is a field identifier like any other -- it must be
checked against the fixed part the same way a variant field already is,
seventy lines away in the same file (walkVariantFields).

The tag-field check used to be silently SKIPPED instead of diagnosed when
the name collided with a fixed field: it kept the fixed field and dropped
the tag out of Sema's flattened field list entirely, while codegen still
reserved storage for the discriminator.  Sema's layout then disagreed with
codegen's, and the offset cross-check gate aborted the compiler with no
file and no line -- a user's mistake reported as an internal error, which
is still the wrong answer: it must be rejected as a plain diagnostic
instead.
*)

program p(output);
type r1 = record a: integer; case a: boolean of true: (x: integer); false: () end;
var v: r1;
begin writeln('ok') end.
