(*
Issue #142.  A schema var (reference) PARAMETER's compiled ABI pointer
addresses the object's BODY directly -- SchemaAccess::schemaActual/
schemaRefOf strip any header before passing the actual on, and
CodeGenProcs.cpp's schema-param binding receives that body pointer as-is
-- never the header emitNewSchema writes in front of a directly-allocated
object.  buildSchemaDIType's header-at-offset-0 struct (issue #122/#130's
own fix) assumed every ExtentVaries schema value it was asked to describe
was such a direct object, so a var parameter's discriminant read where the
body's own first bytes are, and every field read hdrBytes further into the
body than it really sits.

Two shapes of actual reach the same formal here: 'fx', a fixed-extent local
(a SchemaInstance -- CGTypes::llvmTypeOfSemaTypeImpl's own SchemaInstance
case stores it as the body value with no header AT ALL, not even one to
misread the offset of), and 'q^', a genuinely run-time-varying heap object
new() gave a real header to.  CGDebugInfo::declareSchemaParamRef fixes
both the same way: a small debug-only shadow block (real discriminant
values, already correct off the call's own arguments, followed by a
pointer to the real body) that plang_schema_printers.py reads through its
own ".ref"-tagged struct instead of assuming the body sits right after the
header the way a direct object's does.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:40" -ex run -ex "print r" -ex continue -ex "print r" %t 2>&1 | FileCheck %s
*)

program p;
type Rec(n: integer) = record a: array[1..n] of integer; k: integer end;
type RecPtr = ^Rec;

procedure show(var r: Rec);
begin
  writeln(r.n, ' ', r.a[1], ' ', r.a[2], ' ', r.a[3], ' ', r.k)
end;

var
  fx: Rec(3);
  q: RecPtr;
begin
  fx.a[1] := 11; fx.a[2] := 22; fx.a[3] := 33; fx.k := 777;
  show(fx);
  new(q, 3);
  q^.a[1] := 111; q^.a[2] := 222; q^.a[3] := 333; q^.k := 7777;
  show(q^)
end.

(*
CHECK: Rec = [[SEP1:.*]]n = 3, a = [[SEP2:.*]]11, 22, 33[[SEP3:.*]]k = 777
CHECK: Rec = [[SEP4:.*]]n = 3, a = [[SEP5:.*]]111, 222, 333[[SEP6:.*]]k = 7777
*)
