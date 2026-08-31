(*
Turbo Tier 5, Cluster A item 6: New(P, Init(args)) -- the full pipeline.
Allocates memory sized for the pointee object type, stamps its '_vptr' the
same way a directly declared local/global's own vptr is stamped
(CodeGenTurboVirtualDispatch's own tests), THEN calls the named constructor
with the freshly allocated, already-vptr-stamped memory as Self.

The proof that the vptr really was stamped -- not just that the
constructor's own field-setting worked, which would pass even with a
garbage vptr as long as no virtual call is ever made -- is the virtual
'Speak' call made AFTER New(P, Init(...)) returns: dispatching correctly to
TDog's own override, through a pointer whose memory New allocated (not a
directly-declared variable, which item 5 already covers), proves the whole
allocate -> stamp-vptr -> construct -> later-dispatch chain end to end.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  PAnimal = ^TAnimal;
  TAnimal = object
    Name: string[20];
    constructor Init;
    procedure Speak; virtual;
  end;
  PDog = ^TDog;
  TDog = object(TAnimal)
    constructor Init(N: string);
    procedure Speak; virtual;
  end;

constructor TAnimal.Init;
begin
  Name := 'Animal';
end;

procedure TAnimal.Speak;
begin
  writeln(Name, ' makes a generic animal sound');
end;

constructor TDog.Init(N: string);
begin
  inherited Init;
  Name := N;
end;

procedure TDog.Speak;
begin
  writeln(Name, ' says Woof!');
end;

var
  P: PDog;
begin
  New(P, Init('Rex'));
  P^.Speak;
end.

(*
CHECK:Rex says Woof!
*)
