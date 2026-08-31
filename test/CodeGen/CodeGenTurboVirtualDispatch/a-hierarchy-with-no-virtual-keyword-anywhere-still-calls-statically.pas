(*
Turbo Tier 5, Cluster A item 5: no regression in item 4's own static-call
behavior -- a hierarchy that declares no 'virtual' method anywhere (so
Type::VmtSlots is empty at every level, and Type::introducesVptr() is false
everywhere: no `_vptr` field exists in this hierarchy's own storage at all)
must still call every method as a plain, direct call, exactly as item 4
built it.  A call through an ancestor-typed reference here resolves to
whatever the ancestor-chain walk finds STATICALLY -- there is no VMT for it
to dispatch through even if CodeGen wrongly tried to.
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
    procedure Speak;
  end;
  TDog = object(TAnimal)
    procedure Speak;
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
  D.Speak;

  { A non-virtual call through an ancestor-typed pointer resolves to the
    ANCESTOR's own body, statically, even over a descendant's storage --
    confirmed against a local `fpc -Mtp` build: with no 'virtual' anywhere,
    this is ordinary static binding, matching Pascal's default. }
  PA := @TAnimal(D);
  PA^.Speak;

  A.SetName('Generic');
  A.Speak;
end.

(*
CHECK:Rex says Woof!
CHECK-NEXT:Rex makes a generic animal sound
CHECK-NEXT:Generic makes a generic animal sound
*)
