(*
Turbo Tier 5, Cluster A item 5: 'inherited Speak' inside TLeaf.Speak (which
itself overrides TMid.Speak, which overrides TBase.Speak) must reach TMid's
OWN body -- the DIRECT parent -- and must do so STATICALLY, bypassing the
VMT entirely, even though:
  (a) the enclosing call itself arrived via VIRTUAL dispatch (through a
      TBase-typed pointer to the TLeaf instance), and
  (b) TMid's own Speak is ITSELF declared 'virtual'.
A bug that made 'inherited' redispatch through the VMT instead of calling
TMid's body directly would print TLeaf's own line a second time (infinite
recursion in the general case; here Speak has no further 'inherited' call so
it would just repeat "leaf speak" instead of printing "mid speak") --
so the exact three lines below, in order, are the whole proof: virtual
dispatch reaches the override, 'inherited' reaches the direct parent
exactly once, and does not loop or skip past it to TBase.

Cross-checked against a local `fpc -Mtp` build of the equivalent
constructor-based program (plang has no constructor/New codegen yet --
Cluster A item 6's job -- so the cross-check program initializes via a
constructor called directly, `L.Init('Fido')`, rather than this test's own
`SetName`): fpc's own output is byte-for-byte identical to this test's
CHECK lines below (own two lines: virtual dispatch to the leaf override,
then 'inherited' reaching the direct parent's own body exactly once).
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
    procedure Speak; virtual;
  end;
  TMid = object(TBase)
    procedure Speak; virtual;
  end;
  TLeaf = object(TMid)
    procedure Speak; virtual;
  end;

procedure TBase.SetName(N: string);
begin
  Name := N;
end;

procedure TBase.Speak;
begin
  writeln(Name, ': base speak');
end;

procedure TMid.Speak;
begin
  writeln(Name, ': mid speak');
end;

procedure TLeaf.Speak;
begin
  writeln(Name, ': leaf speak');
  inherited Speak;
end;

var
  L: TLeaf;
  PB: ^TBase;
begin
  L.SetName('Fido');
  PB := @TBase(L);
  PB^.Speak;
end.

(*
CHECK:Fido: leaf speak
CHECK-NEXT:Fido: mid speak
*)
