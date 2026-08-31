(*
Issue #509: the expression-context sibling of
inherited-calls-the-direct-parent-statically-and-does-not-redispatch.pas --
the same proof (a 3-level 'virtual' hierarchy, dispatched through an
ANCESTOR-typed pointer so the call itself arrives via the VMT, with the
override's own 'inherited' call required to reach the DIRECT PARENT
statically, bypassing the VMT even though the parent's own Speak is ALSO
'virtual'), just with 'inherited Speak()' used as a VALUE -- fed into string
concatenation to build TLeaf.Speak's own result -- rather than as its own
bare statement.  A bug that made this 'inherited' redispatch through the
VMT instead of calling TMid's body directly would print TLeaf's own line a
second time (infinite recursion in the general case; here Speak has no
further 'inherited' call so it would just repeat "leaf speak" instead of
reaching "mid speak") -- so the exact CHECK line below is the whole proof,
in one line: virtual dispatch reaches TLeaf's override, and TLeaf's own
'inherited Speak()' reaches TMid's direct body exactly once, with the
resulting VALUE (not just the side effect) coming back correctly.

Cross-checked against a local `fpc -Mtp` build of the equivalent
constructor-based program (this test's own plain 'var L: TLeaf;' relies on
plang's own choice to stamp '_vptr' for every object variable at
declaration time, not only through a constructor -- see
inherited-calls-the-direct-parent-statically-and-does-not-redispatch.pas's
own identical comment for why the fpc cross-check instead calls
'L.Init(...)' -- FPC does not stamp '_vptr' without one, and a virtual call
through an uninitialized VMT pointer traps at run time): fpc's own output is
byte-for-byte identical to this test's CHECK line below.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TBase = object
    Name: string[20];
    procedure SetName(N: string);
    function Speak: string; virtual;
  end;
  TMid = object(TBase)
    function Speak: string; virtual;
  end;
  TLeaf = object(TMid)
    function Speak: string; virtual;
  end;

procedure TBase.SetName(N: string);
begin
  Name := N;
end;

function TBase.Speak: string;
begin
  Speak := Name + ': base speak';
end;

function TMid.Speak: string;
begin
  Speak := Name + ': mid speak';
end;

function TLeaf.Speak: string;
var
  S: string;
begin
  S := inherited Speak();
  Speak := Name + ': leaf speak, then [' + S + ']';
end;

var
  L: TLeaf;
  PB: ^TBase;
begin
  L.SetName('Fido');
  PB := @TBase(L);
  WriteLn(PB^.Speak());
end.

(*
CHECK:Fido: leaf speak, then [Fido: mid speak]
*)
