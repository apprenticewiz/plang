(*
Issue #723 regression coverage for the FIX's non-regression side: the
bare-form 'inherited;' signature check (SemaExpr.cpp,
Sema::checkInheritedCall) now compares each parameter's IsConst as well as
IsVar/IsUntyped/type -- this test makes sure that comparison does not turn
into a false positive for the ordinary, legitimate case where const-ness
on a STRUCTURED (record) parameter genuinely MATCHES between a statically
hiding method and the ancestor it hides (TD.Show here has no 'virtual' of
its own -- a true static hide, exactly issue #723's own scenario, just
with matching 'const' on both sides instead of a mismatch), so the bare
'inherited;' still compiles and correctly forwards the caller's own
struct-by-pointer argument (isStructuredForConstByRef, CodeGenProcs.cpp)
to the ancestor.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  rec = record a, b: Integer end;
  TA = object
    procedure Show(const r: rec); virtual;
  end;
  TD = object(TA)
    procedure Show(const r: rec);
  end;

procedure TA.Show(const r: rec);
begin
  writeln('TA.Show ', r.a, ' ', r.b);
end;

procedure TD.Show(const r: rec);
begin
  inherited;
  writeln('TD.Show done');
end;

var
  D: TD;
  V: rec;
begin
  V.a := 7;
  V.b := 9;
  D.Show(V);
end.

(*
CHECK:TA.Show 7 9
CHECK-NEXT:TD.Show done
*)
