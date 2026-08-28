(*
Turbo Pascal 7's Overlay unit manages code overlays for a real-mode DOS
executable too small to fit in memory all at once -- another concept with no
meaning on plang's flat 64-bit address space.  This covers the overlay
manager's callable API (OvrInit, OvrInitEMS, OvrClearBuf, OvrGetBuf,
OvrSetBuf, OvrGetRetry, OvrSetRetry) and its OvrResult status variable.  Not
included, by deliberate judgment call (see DiagnosticSemaKinds.def and
SemaExpr.cpp's RealModeDosNames comment): OvrCodeList/OvrDebugPtr (obscure
internal-use variables) and the ovrOk/ovrError/... integer error-code
constants.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'OvrInit' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrInitEMS' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrClearBuf' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrGetBuf' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrSetBuf' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrGetRetry' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrSetRetry' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'OvrResult' is a real-mode DOS facility and has no meaning under -std=turbo on this target
*)

program p;
var x: integer;
begin
  OvrInit('overlay.ovr');
  OvrInitEMS;
  OvrClearBuf;
  x := OvrGetBuf;
  OvrSetBuf(1000);
  x := OvrGetRetry;
  OvrSetRetry(5);
  x := OvrResult
end.
