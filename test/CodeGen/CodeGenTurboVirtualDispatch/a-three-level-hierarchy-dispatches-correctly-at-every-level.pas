(*
Turbo Tier 5, Cluster A item 5: a 3-level hierarchy (TBase/TMid/TLeaf)
exercising every VMT-slot shape at once through a LEVEL-1-typed pointer to a
LEVEL-3 instance:
  - Speak: overridden at level 2 (TMid) AND AGAIN at level 3 (TLeaf) --
    dispatch through a TBase-typed pointer must reach TLeaf's own body, two
    levels down from the pointer's own static type.
  - Wag: a NEW virtual method introduced at level 2, inherited UNCHANGED by
    level 3 -- dispatch through a TMid-typed pointer to the same TLeaf
    instance must reach TMid's own body (there is no TLeaf override to
    reach).
Both pointers are produced via the same '@Type(Instance)' variable-typecast
idiom the sibling test in this directory explains (item 7's still-missing
pointer covariance, not this item's job).
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
    procedure Wag; virtual;
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

procedure TMid.Wag;
begin
  writeln(Name, ': mid wag');
end;

procedure TLeaf.Speak;
begin
  writeln(Name, ': leaf speak');
end;

var
  L: TLeaf;
  PB: ^TBase;
  PM: ^TMid;
begin
  L.SetName('Fido');

  PB := @TBase(L);
  PB^.Speak;

  PM := @TMid(L);
  PM^.Wag;
end.

(*
CHECK:Fido: leaf speak
CHECK-NEXT:Fido: mid wag
*)
