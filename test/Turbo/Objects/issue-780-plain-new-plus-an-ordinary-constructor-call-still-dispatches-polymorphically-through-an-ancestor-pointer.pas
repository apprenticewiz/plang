(*
Issue #780 follow-up: the fix stamps '_vptr' at the ORDINARY qualified
constructor-call SITE itself (CGProcCall::emitMethodCallStmt, and the
shared emitBoundMethodCall core it now shares that stamp with), using the
call's own RECEIVER type -- not the type that happens to DECLARE the
constructor.  That matters for the real-world idiom this test covers: a
plain 'New' through an ANCESTOR-typed pointer, narrowed to the concrete
DESCENDANT type only at the 'New'/constructor call sites via an explicit
pointer-type cast ('PCircle(p)'), exactly the shape real Borland/FPC's own
"use extended syntax of NEW and DISPOSE" warning covers -- confirmed
against a local `fpc -Mtp` build, byte-for-byte including that warning
(this file's own leading pragma matches).

TCircle.Init is deliberately NOT virtual (a constructor can never be
virtual -- err_object_virtual_constructor), so the polymorphism under test
is entirely in the LATER 'p^.Area' call, dispatched through the
ANCESTOR-typed 'p' after construction -- proving the vptr slot the ordinary
'PCircle(p)^.Init(...)' call stamped really does hold TCircle's own VMT,
not TShape's (a wrong/missing stamp here would either trap on the nil-VMT
guard, exactly like the plain-repro test just above, or -- had the stamp
mistakenly used Owner, the DECLARING type of a hypothetically-inherited
Init, instead of the receiver's own cast-narrowed type -- silently
dispatch to TShape.Area instead, printing 0.00000 instead of the circle's
real area).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program PlainNewOrdinaryCtorPolymorphic;

type
  PShape = ^TShape;
  TShape = object
    constructor Init;
    function Area: Real; virtual;
  end;

  PCircle = ^TCircle;
  TCircle = object(TShape)
    Radius: Real;
    constructor Init(R: Real);
    function Area: Real; virtual;
  end;

constructor TShape.Init;
begin
end;

function TShape.Area: Real;
begin
  Area := 0.0;
end;

constructor TCircle.Init(R: Real);
begin
  inherited Init;
  Radius := R;
end;

function TCircle.Area: Real;
begin
  Area := 3.14159 * Radius * Radius;
end;

var
  p: PShape;
begin
  New(PCircle(p));           { plain New through a cast-narrowed pointer }
  PCircle(p)^.Init(2.0);     { ordinary qualified constructor call }
  writeln(p^.Area:0:5);      { virtual dispatch through the ANCESTOR-typed p }
  Dispose(PCircle(p));
end.

(*
CHECK:12.56636
*)
