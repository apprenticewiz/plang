(*
Turbo Tier 5, Cluster A item 1: the whole VMT slot-assignment algorithm in
one program (Sema::resolveObjectType, SemaType.cpp) -- confirmed against a
local fpc -Mtp build. TDog's own VmtSlots starts as a verbatim copy of
TAnimal's (same method, same index -- Done stays at slot 1, TAnimal still
its ImplementingType, since TDog never redeclares it), then:
  - overriding Speak (declared 'virtual' in both, identical signature)
    replaces slot 0's ImplementingType with TDog, keeping the SAME index;
  - Bark, a plain (non-virtual) NEW method, gets no slot at all.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program OutOfLineBodies;

type
  TAnimal = object
    Name: string;
    constructor Init(N: string);
    procedure Speak; virtual;
    destructor Done; virtual;
  end;

  TDog = object(TAnimal)
  private
    Breed: string;
  public
    procedure Speak; virtual;
    procedure Bark;
  end;

constructor TAnimal.Init(N: string);
begin
  Name := N;
end;

procedure TAnimal.Speak;
begin
end;

destructor TAnimal.Done;
begin
end;

procedure TDog.Speak;
begin
end;

procedure TDog.Bark;
begin
end;

begin
end.

(*
CHECK:(vmt TAnimal
CHECK-NEXT:  (fields (Name string[255] public))
CHECK-NEXT:  (methods (Init constructor) (Speak procedure virtual slot=0) (Done destructor virtual slot=1))
CHECK-NEXT:  (slots (0 Speak TAnimal) (1 Done TAnimal)))
CHECK-NEXT:(vmt TDog (ancestor TAnimal)
CHECK-NEXT:  (fields (Name string[255] public) (Breed string[255] private))
CHECK-NEXT:  (methods (Speak procedure virtual slot=0) (Bark procedure))
CHECK-NEXT:  (slots (0 Speak TDog) (1 Done TAnimal)))
*)
