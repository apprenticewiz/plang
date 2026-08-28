(*
Issue #409's other shape: the nested instantiation's actual is a fixed
LITERAL rather than a form over the enclosing discriminant -- `x:
inner(50)` inside `outer`'s body, for `inner(m: small) = array[1..m] of
integer` with `small = 1..3`.  50 is out of small's declared range and gets
no diagnostic at all, compile-time or run-time, for exactly the same
underlying reason as the enclosing-discriminant-derived case (see this
directory's a-nested-schema-instantiation-fields-discriminant-is-range-
checked.pas): emitNewSchema's own range-check loop only ever looked at the
arguments passed directly to new().

This exercises a different branch of the fix than that sibling test: Sema
never builds a closed ActualForm for a literal actual (SchemaTypeNode::
ActualForms stays empty -- see rtSizeOfTypeNode's own #393 comment on
exactly this), so the range check has to be applied to the value already
sitting in the nested instantiation's own resolved SchemaDiscs rather than
to one recomputed through emitExtentForm.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 50 out of range 1..3
*)

program p(output);
type small = 1..3;
     inner(m: small) = array[1..m] of integer;
     outer(n: integer) = record x: inner(50); k: integer end;
var q: ^outer;
begin
  new(q, 1);
  q^.k := 99;
  writeln('k=', q^.k:1);
  writeln('done')
end.
