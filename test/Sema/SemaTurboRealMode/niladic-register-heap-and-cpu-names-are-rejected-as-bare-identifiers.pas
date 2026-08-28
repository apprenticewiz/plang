(*
Real Turbo Pascal spells its niladic real-mode functions and variables
without parentheses -- CSeg, DSeg, SSeg, SPtr, PrefixSeg, Test8086, Test8087,
MemAvail, MaxAvail, HeapOrg, HeapPtr, HeapEnd, FreeList are all read as a
bare identifier, `x := CSeg` rather than `x := CSeg()`.  That is the plain
IdentExpr path (checkIdent, SemaExpr.cpp) an ordinary variable reference
takes, distinct from the parenthesized-call path (checkCallExpr) Seg/Ofs/Ptr
take and from the statement-call path (checkCallStmt) Mark/Release/Intr/...
take.  All thirteen must fire the specific diagnostic here too.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'CSeg' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'DSeg' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'SSeg' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'SPtr' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'PrefixSeg' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Test8086' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Test8087' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'MemAvail' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'MaxAvail' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'HeapOrg' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'HeapPtr' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'HeapEnd' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'FreeList' is a real-mode DOS facility and has no meaning under -std=turbo on this target
*)

program p;
var x: integer;
begin
  x := CSeg;
  x := DSeg;
  x := SSeg;
  x := SPtr;
  x := PrefixSeg;
  x := Test8086;
  x := Test8087;
  x := MemAvail;
  x := MaxAvail;
  x := HeapOrg;
  x := HeapPtr;
  x := HeapEnd;
  x := FreeList
end.
