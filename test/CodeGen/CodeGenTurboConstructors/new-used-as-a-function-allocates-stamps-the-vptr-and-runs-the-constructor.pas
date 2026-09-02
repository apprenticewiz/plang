(*
Issue #622: 'p := New(PtrType, Ctor(args))' -- New used as a FUNCTION
(real Borland/FPC's own "extended syntax"), the canonical TP7 polymorphic-
allocation idiom.  This is new-init-allocates-stamps-the-vptr-and-runs-the-
constructor.pas's own sibling, with the statement form 'New(P, Init(...))'
replaced by the function form 'P := New(PDog, Init(...))' -- proving the
same allocate -> stamp-vptr -> construct -> later-dispatch chain end to
end when New is used as an expression rather than a statement.  Confirmed
against a local fpc -Mtp build (identical output).
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
  P := New(PDog, Init('Rex'));
  P^.Speak;
end.

(*
CHECK:Rex says Woof!
*)
