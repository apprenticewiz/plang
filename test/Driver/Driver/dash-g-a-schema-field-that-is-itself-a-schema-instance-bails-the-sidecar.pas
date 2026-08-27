(*
Issue #143.  CGDebugInfo::recordSchemaLayoutForScript's two exclusion guards
only ever look at fd.Type->ExtentLow/ExtentHigh (for a non-array field) or
at->ExtentLow/ExtentHigh (for an array field's element) to decide whether a
field is out of scope for the sidecar this function writes (see
dash-g-schema-printer-fixes-a-field-after-a-varying-one.pas for what the
sidecar is for).  A field whose own type is itself another schema
instantiation -- 'f: inner(n)' below -- is ALSO out of scope (its real size
depends on inner's own discriminant, not on any constant the probe used),
but a SchemaTypeNode field never carries ExtentLow/ExtentHigh at all --
those belong only to a string capacity / subrange / array-bound denoter.
Neither guard fired for it, so it fell through to the generic "scalar"
branch, which recorded FieldLLTy's compile-time-probe size (inner's OWN
probe body, always instantiated with discriminant 1 --  see
SchemaTypeRegistry's probe-binding comment) as if it were the field's real,
run-time size.

For the two-integer 'inner' below that probe size is 16 bytes; feeding that
wrong 16 straight to plang_schema_printers.py's _layout_walk, which treats
it as an ordinary scalar and hands it to int.from_bytes(..., 16 bytes) for
what is really an 8-byte-aligned struct read past the end of the object,
crashed the pretty-printer outright with a Python OverflowError -- worse
than not having the sidecar at all.

The fix bails the WHOLE containing schema's recording (same as the existing
variant-part and varying-non-array-field guards just above it) the moment a
field's type -- or, for an array field, its element type -- is a
SchemaTypeNode, so 'outer' below gets no sidecar entry, and gdb/the
pretty-printer fall back to plain, non-crashing DWARF the same way they
already do for the other documented out-of-scope shapes.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: cat %s.plang-schemas.json | FileCheck --check-prefix=SIDECAR %s
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:57" -ex run -ex "print q^" %t 2>&1 | FileCheck --check-prefix=GDB %s
*)

program p;
type inner(m: integer) = record v, w: integer end;
     outer(n: integer) = record
        a: array[1..n] of integer;
        f: inner(n);
        k: integer
     end;
     outerptr = ^outer;
var q: outerptr;
begin
  new(q, 3);
  q^.a[1] := 11; q^.a[2] := 22; q^.a[3] := 33;
  q^.f.v := 99; q^.f.w := 100;
  q^.k := 777;
  writeln(q^.a[3], ' ', q^.f.v, ' ', q^.f.w, ' ', q^.k)
end.

(*
SIDECAR-NOT: "outer"

GDB-NOT: OverflowError
GDB-NOT: Python Exception
GDB: {n = 3,
*)
