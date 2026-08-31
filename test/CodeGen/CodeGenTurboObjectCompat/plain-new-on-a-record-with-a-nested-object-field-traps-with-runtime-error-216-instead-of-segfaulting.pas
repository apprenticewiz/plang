(*
Issue #514's second repro: the object type is not the pointee itself but
NESTED inside a plain record field (THolder.Pet: TDog) that plain New(p)
allocates.  Before this fix this segfaulted exactly like the bare-pointer
sibling test in this same directory (unmodified main: exit 139, core
dumped) -- plang_new's calloc (runtime/plang_sys.cpp) zero-fills the WHOLE
allocated block regardless of nesting depth, so Pet's own `_vptr` slot
was already reliably NULL (never garbage) even pre-fix; the missing piece
was only ever the virtual-dispatch call site's own check, not anything
about how plain New(p) allocates.  Real Borland/FPC traps this identically
-- confirmed against a local `fpc -Mtp` build: "Runtime error 216", exit
216, no core dump (notably with no "use extended syntax" warning this
time, since the pointee here, THolder, is a record rather than an object
type itself -- fpc's warning is specific to the direct pointee, but the
runtime trap is not).
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %checkexit 216 %run %t.O0 > %t.O0.out 2> %t.O0.err
RUN: FileCheck %s < %t.O0.out
RUN: FileCheck --check-prefix=ERR %s < %t.O0.err
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %checkexit 216 %run %t.O2 > %t.O2.out 2> %t.O2.err
RUN: FileCheck %s < %t.O2.out
RUN: FileCheck --check-prefix=ERR %s < %t.O2.err
*)

program plainnew2;
type
  TAnimal = object
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;
  THolder = record
    Pet: TDog;
  end;
  PHolder = ^THolder;
var
  P: PHolder;

procedure TAnimal.Speak;
begin
  writeln('Animal speaks');
end;

procedure TDog.Speak;
begin
  writeln('Dog speaks');
end;

begin
  New(P);
  writeln('about to dispatch through a nested unstamped vptr');
  P^.Pet.Speak;
  writeln('unreachable');
end.

(*
CHECK:about to dispatch through a nested unstamped vptr
CHECK-NOT:unreachable
ERR:Runtime error 216
*)
