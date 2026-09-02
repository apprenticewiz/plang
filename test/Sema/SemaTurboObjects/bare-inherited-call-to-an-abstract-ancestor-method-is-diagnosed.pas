(*
Issue #574's own bare-form sibling: 'inherited;' (no method name written)
means "the same method this activation itself is overriding" -- resolved to
CurrentProc's own name in checkInheritedCall (SemaExpr.cpp) -- so it hits
the exact same abstract-method check the explicit-name form does, before
ever reaching the bare form's own signature-matching logic.  Confirmed
against a local fpc -Mtp build: refused at compile time here too.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BareInheritedAbstractMethod;

type
  TA = object
    constructor Init;
    procedure M; virtual; abstract;
  end;
  TB = object(TA)
    procedure M; virtual;
  end;

constructor TA.Init;
begin
end;

procedure TB.M;
begin
  inherited;
end;

begin
end.

(*
CHECK: error: cannot call abstract method 'TA.M' via 'inherited'
*)
