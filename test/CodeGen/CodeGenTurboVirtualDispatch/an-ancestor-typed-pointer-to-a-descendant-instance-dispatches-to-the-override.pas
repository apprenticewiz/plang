(*
Turbo Tier 5, Cluster A item 5: the single most important behavioral proof
for this item -- a POINTER TYPED AS THE ANCESTOR (PA: ^TAnimal) actually
holding the ADDRESS OF A DESCENDANT instance (D: TDog), and a virtual call
through that pointer (PA^.Speak) reaching TDog's own override, not
TAnimal's -- even though PA's STATIC type is only ever ^TAnimal.  A naive
implementation that accidentally fell back to a static/direct call (this
item's whole risk, per its own task description) would compile fine and
print TAnimal's own line instead; only a genuine indirect call through the
receiver's own `_vptr` and the VMT slot gets this right.

Turbo Tier 5, Cluster A item 7 added pointer/var-parameter COVARIANCE
(assigning a '^TDog' expression straight to a '^TAnimal' variable), so this
now uses the natural `PA := @D;` idiom directly -- confirmed against a local
fpc -Mtp build (cov1.pas) that real Turbo/FPC accepts exactly this, and
Sema::isAssignCompatible's own Pointer case now walks the same Type::Parent
ancestor chain every other Tier-5 lookup already does to allow it.  This
replaces the same-size variable-typecast workaround (`@TAnimal(D)`) every
prior item's own verification had to fall back on.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TAnimal = object
    Name: string[20];
    procedure SetName(N: string);
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;

procedure TAnimal.SetName(N: string);
begin
  Name := N;
end;

procedure TAnimal.Speak;
begin
  writeln(Name, ' makes a generic animal sound');
end;

procedure TDog.Speak;
begin
  writeln(Name, ' says Woof!');
end;

var
  D: TDog;
  A: TAnimal;
  PA: ^TAnimal;
begin
  D.SetName('Rex');
  PA := @D;
  PA^.Speak;

  A.SetName('Generic');
  PA := @A;
  PA^.Speak;
end.

(*
CHECK:Rex says Woof!
CHECK-NEXT:Generic makes a generic animal sound
*)
