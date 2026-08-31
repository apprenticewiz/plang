(*
Issue #511's own fix (stampFieldVptrs, CodeGenProcs.cpp) walks a record's
fields recursively looking for a TypeKind::Object member, however deeply
nested -- not just one directly inside the outermost record.  This proves
the recursion actually recurses: TOuter holds a TMiddle by value, which
itself holds the object-typed field (Pet) two levels down from the declared
variable.  A version of the fix that only checked the OUTERMOST record's
own immediate fields (rather than recursing into a nested record field
that is itself composite) would miss Pet entirely and this would still
segfault.  Confirmed against a local `fpc -Mtp` build: identical single
'woof' line, exit 0.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program nested2;
type
  TAnimal = object
    constructor Init;
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;
  TMiddle = record
    Pet: TDog;
    Count: Integer;
  end;
  TOuter = record
    Mid: TMiddle;
    Tag: Integer;
  end;
constructor TAnimal.Init; begin end;
procedure TAnimal.Speak; begin WriteLn('generic'); end;
procedure TDog.Speak; begin WriteLn('woof'); end;
var
  O: TOuter;
begin
  O.Mid.Pet.Init;
  O.Mid.Pet.Speak;
end.

(*
CHECK:woof
*)
