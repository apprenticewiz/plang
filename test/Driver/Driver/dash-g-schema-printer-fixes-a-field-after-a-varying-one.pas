(*
Issue #130.  buildSchemaDIType's own fix (issue #122) makes the discriminant
and every field AT OR BEFORE a varying-extent field read correctly under
gdb, but a FIXED field declared AFTER a varying one still reads its offset
off the probe's approximated extent, not the real one -- DWARF itself has
no implementation of a computed member ADDRESS to express this correctly
(confirmed directly from LLVM's own DwarfUnit.cpp: an expression-typed
member offset always becomes DW_AT_data_bit_offset, a bitfield-only
attribute; confirmed empirically too -- gdb 17.2 crashes trying to print a
member built that way, worse than the documented approximation).

share/plang/gdb/plang_schema_printers.py sidesteps DWARF for exactly this
case: plang (compiled here with -g) writes a sidecar,
<this file>.plang-schemas.json, describing the schema's real layout as
data; the pretty-printer walks it against live memory at print time,
computing the correct address for 'k' (declared after the varying 'a')
directly, the same way SchemaLayoutEngine's own run-time walk would.

'a' (the varying field itself) is unaffected either way -- both the DWARF
type and the pretty-printer already agree it holds the right values, this
test's whole point is 'k', the field DWARF alone still gets wrong.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:43" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
*)

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

(*
CHECK: Rec = [[SEP1:.*]]n = 3, a = [[SEP2:.*]]11, 22, 33[[SEP3:.*]]k = 777
*)
