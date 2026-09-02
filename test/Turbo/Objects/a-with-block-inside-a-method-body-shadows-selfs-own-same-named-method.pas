(*
Issues #571/#623 combined: an unqualified method call may need to resolve
against EITHER the enclosing method body's own implicit Self (#571) OR an
active 'with objInstance do' block's own object (#623), and the two can be
active at once -- 'with other do Who;' written INSIDE a method body whose
OWN type also declares 'Who' must reach 'other's own Who, not Self's,
exactly the ordinary Pascal with-shadowing priority a with-bound FIELD of
the same name already gets.  Regression coverage for the priority order
between Sema's own two implicit-receiver mechanisms (pushMethodSelfScope
and pushWithScope), not just that each resolves something in isolation.
Confirmed against a local `fpc -Mtp` build.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program WithShadowsSelf;

type
  TB = object
    m: Integer;
    constructor Init(v: Integer);
    procedure Who;
  end;
  TA = object
    n: Integer;
    constructor Init(v: Integer);
    procedure Who;
    procedure Test(var other: TB);
  end;

constructor TB.Init(v: Integer); begin m := v; end;
procedure TB.Who; begin writeln('TB.Who m=', m); end;

constructor TA.Init(v: Integer); begin n := v; end;
procedure TA.Who; begin writeln('TA.Who n=', n); end;

procedure TA.Test(var other: TB);
begin
  writeln('bare Who (no with active):');
  Who;
  writeln('with other do Who (must shadow Self):');
  with other do Who;
  writeln('bare Who again (with scope closed, back to Self):');
  Who;
end;

var
  A: TA;
  B: TB;
begin
  A.Init(1);
  B.Init(2);
  A.Test(B);
end.

(*
CHECK:bare Who (no with active):
CHECK-NEXT:TA.Who n=1
CHECK-NEXT:with other do Who (must shadow Self):
CHECK-NEXT:TB.Who m=2
CHECK-NEXT:bare Who again (with scope closed, back to Self):
CHECK-NEXT:TA.Who n=1
*)
