(*
Issue #730 companion: the fix that lets an unqualified CALL fall back from a
self-scope FIELD symbol to a same-named sibling METHOD (see
an-unqualified-call-to-a-method-that-shares-a-name-with-an-inherited-field-
resolves-to-the-method.pas, test/Turbo/Objects/) is gated on
Symbol::IsSelfScopeField -- an ordinary local variable that is neither a
field nor a method never sets that flag, so calling one (statement context,
checkCallStmt, and expression context, checkCallExpr) must still be
refused by the ordinary err_not_callable diagnosis exactly as before,
whether or not it happens to be declared inside an object method body where
the self-scope-field fallback is active.
*)

(*
RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program CallingAPlainNonFieldVariableIsStillNotCallable;

type
  TBase = object
    X: Integer;
    constructor Init;
  end;
  TChild = object(TBase)
    constructor Init;
    procedure X;
    procedure BadStmt;
    procedure BadExpr;
  end;

constructor TBase.Init; begin X := 42; end;
constructor TChild.Init; begin inherited Init; end;
procedure TChild.X; begin writeln('TChild.X method called'); end;

procedure TChild.BadStmt;
var
  Y: Integer;
begin
  Y;
end;

procedure TChild.BadExpr;
var
  Y, Z: Integer;
begin
  Z := Y(5);
end;

begin
end.

(*
CHECK: error: 'Y' is not callable
CHECK: error: 'Y' is not callable
*)
