(*
Turbo Tier 5, Cluster A item 6: the ordinary, no-'Fail'-anywhere path --
New(P, Init(args)) allocates, constructs, and leaves P non-nil with every
field the constructor set intact; confirms no regression from item 6's own
'Fail'/success-flag ABI change (every constructor now returns a hidden i1
rather than void -- CodeGenImpl.h's curCtorOkAlloca) when 'Fail' is never
actually called on any path.  Also exercises the bare, no-parens callee
form for a zero-argument constructor ('New(P, Init)', not 'New(P, Init())')
-- confirmed legal, and the common real-world spelling, against a local
`fpc -Mtp` build.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  PCounter = ^TCounter;
  TCounter = object
    N: integer;
    constructor Init;
    procedure Bump;
    procedure Show;
  end;

constructor TCounter.Init;
begin
  writeln('Init entered');
  N := 100;
  writeln('Init leaving normally');
end;

procedure TCounter.Bump;
begin
  N := N + 1;
end;

procedure TCounter.Show;
begin
  writeln('N=', N);
end;

var
  P: PCounter;
begin
  New(P, Init);
  if P = nil then
    writeln('P is nil (WRONG)')
  else
    writeln('P is not nil');
  P^.Bump;
  P^.Bump;
  P^.Show;
  Dispose(P);
end.

(*
CHECK:Init entered
CHECK-NEXT:Init leaving normally
CHECK-NEXT:P is not nil
CHECK-NEXT:N=102
*)
