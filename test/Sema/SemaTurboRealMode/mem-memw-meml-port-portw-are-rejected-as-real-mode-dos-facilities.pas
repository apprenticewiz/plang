(*
Turbo Pascal 7's Mem/MemW/MemL raw-memory arrays and Port/PortW raw I/O-port
arrays describe real-mode DOS hardware access; plang's target is a flat
64-bit address space with no such thing.  None of these five names is
declared as a Symbol anywhere (they are not reserved words -- see the
user-declaration-wins test in test/Driver/Turbo), so an undeclared use of one
reaches ordinary name resolution, finds nothing, and only THEN is refused by
name: checkIdent (SemaExpr.cpp), since `Mem[0]` etc. is an IndexExpr over a
plain identifier, an expression-context use exactly like a bare variable
reference is.  Confirms the specific diagnostic fires here, not a generic
"undefined identifier".
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'Mem' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'MemW' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'MemL' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Port' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'PortW' is a real-mode DOS facility and has no meaning under -std=turbo on this target
*)

program p;
var x: integer;
begin
  x := Mem[0];
  x := MemW[0];
  x := MemL[0];
  x := Port[0];
  x := PortW[0]
end.
