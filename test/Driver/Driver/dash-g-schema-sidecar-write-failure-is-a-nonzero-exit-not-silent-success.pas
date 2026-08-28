(*
Issue #396.  CGDebugInfo::writeSchemaDebugScript, the -g schema-printer
sidecar JSON writer share/plang/gdb/plang_schema_printers.py reads
(<source file>.plang-schemas.json), used to be "std::ofstream Out(SidecarPath);
if (Out) Out << J;" -- an open failure (a full disk, a permissions error,
here a read-only directory) was silently ignored, and a write failure after
a successful open was never checked at all either way, so the compile always
reported the same clean success while the sidecar itself was missing or
truncated.  A later gdb session using plang_schema_printers.py then either
fell back to raw values or misbehaved with nothing to say the sidecar write
is what actually failed.

Same root cause, and same "check the write, fail loud" fix, as issue #246
(Frontend.cpp's own -o output writer) and issue #397 (its .pmi writer) --
this is the same pattern in a third writer, CGDebugInfo.cpp's sidecar.

Uses the same technique as
test/Module/SeparateCompilation/an-unwritable-pmi-directory-is-diagnosed-not-silently-skipped.pas
(issue #397's own sibling I/O-failure case): a read-only directory makes the
sidecar's open() fail with EACCES, the only portable way to force a write
failure without a genuinely full disk.  The source file itself only needs to
be READ from that directory, which chmod 555 (r-x) still allows -- only
creating the new sidecar file inside it fails.

The schema type below has to actually reach CGDebugInfo::recordSchemaLayoutForScript
(an ExtentVaries record, instantiated with 'new') so schemaScriptEntries_ is
non-empty and writeSchemaDebugScript does not just no-op before ever trying
to open anything.
*)

(*
RUN: split-file %s %t.dir
RUN: chmod 555 %t.dir/rodir
RUN: not %plang -g -std=iso10206 %t.dir/rodir/schema.pas -o %t.dir/schema.out 2> %t.err
RUN: chmod 755 %t.dir/rodir
RUN: FileCheck %s < %t.err
RUN: test ! -e %t.dir/rodir/schema.pas.plang-schemas.json
RUN: test ! -e %t.dir/schema.out
*)

(*
CHECK: cannot open -g schema sidecar file
CHECK-SAME: schema.pas.plang-schemas.json
*)

//--- rodir/schema.pas
program p;
type Rec(n: integer) = record a: array[1..n] of integer; k: integer end;
type RecPtr = ^Rec;
var q: RecPtr;
begin
  new(q, 3);
  q^.a[1] := 11;
  q^.a[2] := 22;
  q^.a[3] := 33;
  q^.k := 777;
  writeln(q^.a[3], ' ', q^.k)
end.
