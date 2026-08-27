(*
Issue #140.  CGDebugInfo::recordSchemaLayoutForScript used to key its sidecar
entries purely by the schema type's bare declared NAME (schemaScriptEntries_
[T.Name]), on the assumption that two schemas sharing a name always share a
body.  False: two different procedures (as here) -- or two different
modules -- can each declare their own schema type under the identical name
with a completely different field layout.  Only the FIRST one recorded used
to survive in the sidecar; the second was silently dropped, and
share/plang/gdb/plang_schema_printers.py applied the first schema's layout
to the second schema's live objects too, with no warning.

ProcA's Rec and ProcB's Rec share the name "Rec" but not the body: ProcA's
has a leading varying array then a trailing scalar; ProcB's has a leading
scalar, then a differently-sized varying array, then a trailing scalar.  If
ProcB's own layout were dropped in favor of ProcA's (the old bug), every
field ProcB's breakpoint prints would read from the wrong offset -- 'e'
would print (mis-labeled 'a') the two-int array 100, 44 instead of the
scalar 100, and 'c' would read stale-offset garbage instead of 999.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:42" -ex "break %s:55" -ex run -ex "print q^" -ex continue -ex "print q^" %t 2>&1 | FileCheck %s
*)

program p;

procedure ProcA;
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
end;

procedure ProcB;
type Rec(n: integer) = record e: integer; b: array[1..n] of integer; c: integer end;
type RecPtr = ^Rec;
var q: RecPtr;
begin
  new(q, 2);
  q^.e := 100;
  q^.b[1] := 44;
  q^.b[2] := 55;
  q^.c := 999;
  writeln(q^.e, ' ', q^.b[2], ' ', q^.c)
end;

begin
  ProcA;
  ProcB
end.

(*
CHECK: Rec = [[SEP1:.*]]n = 3, a = [[SEP2:.*]]11, 22, 33[[SEP3:.*]]k = 777
CHECK: Rec = [[SEP4:.*]]n = 2, e = 100, b = [[SEP5:.*]]44, 55[[SEP6:.*]]c = 999
*)
