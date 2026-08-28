(*
Issue #416.  Issue #143's own fix (see
dash-g-a-schema-field-that-is-itself-a-schema-instance-bails-the-sidecar.pas)
made CGDebugInfo::recordSchemaLayoutForScript bail its whole recording when a
field's type -- or an array field's element type -- is DIRECTLY a
SchemaTypeNode.  It missed a sibling shape: an ORDINARY (non-schema)
RecordTypeNode field that itself, recursively, contains a member whose
extent was probed against the ENCLOSING schema's own discriminant.  'f'
below is exactly that -- an inline record, not a schema instantiation, whose
own 'arr' field is 'array[1..n]', n being outer's discriminant.  Sema's
ProbeDiscNames_ propagates into every nested resolveTypeImpl call reached
while resolving outer's body (see SemaType.cpp), so 'arr' gets its own
ExtentLow/ExtentHigh set exactly like a top-level varying array field would
-- but neither of #143's guards ever looks past fd.Type/at->Element ONE
level deep, so 'f' fell through to the generic "scalar" branch, which
recorded FieldLLTy's compile-time-PROBE size (every discriminant pinned to
1, so arr held a single element -- see SchemaTypeRegistry's probe-binding
comment) as if it were 'f''s real, run-time-constant size: 16 bytes, when
the real size at n=3 is 32.

Feeding that wrong size straight to plang_schema_printers.py's _layout_walk
put 'z' (the field declared after 'f') at the wrong offset entirely and
crashed the pretty-printer outright with a Python OverflowError trying to
format 'f' itself as a raw 16-byte scalar -- the identical crash class
issue #143 already fixed for a directly-schema-typed field, reached here
through the recursive, nested-record shape instead.

The fix recurses into an ordinary nested record's own fields (and,
transitively, any record nested further inside those) for the same
schema-typed-member / variant-part / probed-extent shapes the caller
already bails on at the top level, so 'outer' below gets no sidecar entry
either, and gdb/the pretty-printer fall back to plain, non-crashing DWARF
the same way they already do for the directly-schema-typed case.

Unlike dash-g-a-schema-field-that-is-itself-a-schema-instance-bails-the-
sidecar.pas (#143's own test), 'outer' is the ONLY schema type this source
file ever reaches -- there is no separate, independently-recorded schema
instantiation (that test's 'inner(1)' is one, reached through a genuinely
different codepath: a nested schema-typed field's OWN probe-bound instance
gets its own buildSchemaDIType call, independent of the containing schema)
to keep a sidecar file around once 'outer' bails.  So the bail here is
checked directly against the sidecar file's own existence, not against its
content -- recordSchemaLayoutForScript's caller (writeSchemaDebugScript)
never even opens the file when nothing was ever recorded for it.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: test ! -e %s.plang-schemas.json
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:72" -ex run -ex "print q^" %t 2>&1 | FileCheck --check-prefix=GDB %s
*)

program p;
type outer(n: integer) = record
        f: record
             arr: array[1..n] of integer;
             k: integer
           end;
        z: integer
     end;
     outerptr = ^outer;
var q: outerptr;
begin
  new(q, 3);
  q^.f.arr[1] := 11; q^.f.arr[2] := 22; q^.f.arr[3] := 33;
  q^.f.k := 99;
  q^.z := 777;
  writeln(q^.f.arr[3], ' ', q^.f.k, ' ', q^.z)
end.

(*
GDB-NOT: OverflowError
GDB-NOT: Python Exception
GDB: {n = 3,
*)
