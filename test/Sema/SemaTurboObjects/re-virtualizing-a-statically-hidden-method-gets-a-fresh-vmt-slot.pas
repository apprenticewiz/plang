(*
Issue #620: TA.Speak is virtual (slot 0).  TB.Speak statically HIDES it (no
'virtual' -- see non-virtual-redeclaration-hides-rather-than-overrides-and-
warns.pas) without touching TB's own VmtSlots, which still names TA as
slot 0's own implementation.  TC.Speak is virtual again -- this is a
RE-VIRTUALIZATION, not an override of anything: the nearest ancestor
DECLARATION of the name 'Speak' (TB's own hide) is not itself virtual, so
TC.Speak must get a brand-new slot (1), leaving slot 0 exactly as TB left
it (still TA).  The bug this regresses: matching by NAME against whatever
VmtSlots entry happens to exist anywhere in the inherited table (rather
than against the nearest ancestor DECLARATION's own virtual-ness) found
TA's stale slot-0 entry straight through TB's hide and let TC silently take
it over -- confirmed via this exact dump showing slot 1 for TC.Speak, and
slot 0 still crediting TA -- see the companion runtime test (test/Turbo/
Objects/) for the observable dispatch consequence through an ancestor-typed
pointer.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s 2> %t.err | FileCheck --strict-whitespace --match-full-lines %s
RUN: FileCheck --check-prefix=WARN %s < %t.err
*)

program ReVirtualizeAfterHide;

type
  TA = object
    procedure Speak; virtual;
  end;
  TB = object(TA)
    procedure Speak;
  end;
  TC = object(TB)
    procedure Speak; virtual;
  end;

procedure TA.Speak;
begin
end;

procedure TB.Speak;
begin
end;

procedure TC.Speak;
begin
end;

begin
end.

(*
WARN: warning: method 'Speak' hides the inherited method of the same name; declare 'virtual' to override it instead

CHECK:(vmt TA
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure virtual slot=0))
CHECK-NEXT:  (slots (0 Speak TA)))
CHECK-NEXT:(vmt TB (ancestor TA)
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure))
CHECK-NEXT:  (slots (0 Speak TA)))
CHECK-NEXT:(vmt TC (ancestor TB)
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure virtual slot=1))
CHECK-NEXT:  (slots (0 Speak TA) (1 Speak TC)))
*)
