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

Pointer/var-parameter COVARIANCE (assigning a '^TDog' expression straight to
a '^TAnimal' variable) is Cluster A item 7's job, not yet implemented -- so
this gets an ancestor-typed POINTER onto the descendant's own storage via
Turbo's variable typecast idiom instead (`@TAnimal(D)`), which reinterprets
D's own storage in place (legal here because TDog adds no fields of its own,
so TAnimal(D) satisfies the same-size rule checkTypeCast enforces) rather
than converting anything -- PA ends up pointing at the exact same bytes `@D`
would, with the exact same real vptr in them, just without needing item 7's
still-missing implicit upcast.  Confirmed this is the intended workaround
by first trying a direct `PA := @D;`, which plang currently refuses with
"cannot assign '^TDog' to variable of type '^TAnimal'" (item 7's own gap).
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
  PA := @TAnimal(D);
  PA^.Speak;

  A.SetName('Generic');
  PA := @A;
  PA^.Speak;
end.

(*
CHECK:Rex says Woof!
CHECK-NEXT:Generic makes a generic animal sound
*)
