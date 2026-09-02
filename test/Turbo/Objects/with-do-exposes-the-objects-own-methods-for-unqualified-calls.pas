(*
Issue #623: 'with obj do' exposed the object's own FIELDS for unqualified
access (pushWithScope's Object branch, SemaStmt.cpp) but never its own
METHODS, so an unqualified method call inside a with-block -- 'Speak;'
(statement, no parens), 'GetX()' (expression, with parens) -- failed to
resolve ('undefined procedure'/'undefined function'), even OUTSIDE any
method body (a distinct root cause from issue #571's implicit-Self-in-a-
method-body gap: pushWithScope's own gap, not pushMethodSelfScope's).
Also covers virtual dispatch through the with-target, and that a
DIFFERENT object type's with-block correctly does NOT expose an unrelated
name.  Confirmed against a local `fpc -Mtp` build: all of this compiles
and runs identically there.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program WithExposesMethods;

type
  TA = object
    x: Integer;
    constructor Init(v: Integer);
    procedure Speak; virtual;
    function GetX: Integer;
  end;
  TD = object(TA)
    procedure Speak; virtual;
  end;

constructor TA.Init(v: Integer); begin x := v; end;
procedure TA.Speak; begin writeln('  -> TA.Speak'); end;
procedure TD.Speak; begin writeln('  -> TD.Speak'); end;
function TA.GetX: Integer; begin GetX := x; end;

var
  D: TD;
begin
  D.Init(42);

  writeln('bare statement-position call inside with:');
  with D do
  begin
    Speak;    { must dispatch virtually to TD.Speak, the runtime type }
  end;

  writeln('parenthesized expression-position call inside with:');
  with D do
    writeln('  GetX() = ', GetX());
end.

(*
CHECK:bare statement-position call inside with:
CHECK-NEXT:  -> TD.Speak
CHECK-NEXT:parenthesized expression-position call inside with:
CHECK-NEXT:  GetX() = 42
*)
