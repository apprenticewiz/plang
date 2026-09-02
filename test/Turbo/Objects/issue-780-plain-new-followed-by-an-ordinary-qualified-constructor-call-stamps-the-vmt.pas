(*
Issue #780: a plain 'New(p)' (not the extended 'New(p, Ctor(...))' syntax)
followed by an ORDINARY, explicitly-qualified constructor call --
'p^.Init;' -- used to leave '_vptr' unstamped, because stampVptr was only
ever reached from a declared variable's own initial-state setup and from
New's EXTENDED-syntax path (emitNewObjectValue) -- never from the plain,
statement-form qualified-method-call path (CGProcCall::emitMethodCallStmt)
when the callee happens to be a constructor.  A later virtual call through
the same pointer (p^.Area below) read the still-NULL '_vptr' slot and
tripped issue #514's own nil-VMT guard, trapping with Runtime error 216
even though this is a completely legal, common real-world TP7 idiom -- real
Borland/FPC accepts it, only warning "use extended syntax of NEW and
DISPOSE for instances of objects" (confirmed against a local `fpc -Mtp`
build; this file's own leading pragma matches).

This is the issue's own repro, reconstructed as written there, with a
virtual Area call added after Init so a trap (rather than silent wrong
output) is exactly what would have happened before the fix.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program PlainNewOrdinaryCtor;

type
  PShape = ^TShape;
  TShape = object
    constructor Init;
    function Area: Real; virtual;
  end;

constructor TShape.Init;
begin
end;

function TShape.Area: Real;
begin
  Area := 0.0;
end;

var
  p: PShape;
begin
  New(p);        { plain New, no extended syntax }
  p^.Init;       { ordinary qualified constructor call, not New(p, Init) }
  writeln(p^.Area:0:5);
  Dispose(p);
end.

(*
CHECK:0.00000
*)
