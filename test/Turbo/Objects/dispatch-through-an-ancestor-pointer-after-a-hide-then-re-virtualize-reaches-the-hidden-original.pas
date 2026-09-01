(*
Issue #620: TA.Speak virtual (slot 0); TB.Speak statically HIDES it (no
'virtual'); TC.Speak is virtual again -- a fresh re-virtualization, per
this directory's own companion Sema-level dump (test/Sema/SemaTurboObjects/
re-virtualizing-a-statically-hidden-method-gets-a-fresh-vmt-slot.pas),
which gets its OWN new slot (1) rather than taking over slot 0 (still
TA's).  The observable consequence: dispatch through a TA-typed pointer to
a live TC instance reaches TA.Speak, NOT TC.Speak -- the static hide really
does break the virtual chain, exactly as real Borland/FPC behaves (a hide
introduces a second, independent virtual identity rather than continuing
the original one).  The bug this regresses (confirmed via -dump-vmt before
the fix): TC took over the ORIGINAL slot 0, so dispatch through this same
TA-typed pointer wrongly reached TC.Speak instead.

Confirmed against a local `fpc -Mtp` build: identical output for the
directly-analogous class-free TP7 object-model program (the same three
"virtual / hide / virtual again" progression through a root-typed pointer).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program DispatchAfterHideThenRevirtualize;

type
  TA = object
    procedure Speak; virtual;
  end;
  TB = object(TA)
    procedure Speak;
  end;
  TC = object(TB)
    procedure Speak; virtual;
  end;

procedure TA.Speak;
begin
  writeln('TA.Speak');
end;

procedure TB.Speak;
begin
  writeln('TB.Speak');
end;

procedure TC.Speak;
begin
  writeln('TC.Speak');
end;

var
  P: ^TA;
  C: TC;
begin
  P := @C;
  P^.Speak;       { must reach TA.Speak: TB's hide broke the virtual chain,
                     and TC's re-virtualization is a NEW slot TA's own VMT
                     layout (what a TA-typed pointer dispatches through)
                     never learns about }
  C.Speak;         { TC's own statically-known type still reaches its own
                     override directly, proving TC.Speak really is defined
                     and callable -- just not through this ancestor slot }
end.

(*
CHECK:TA.Speak
CHECK-NEXT:TC.Speak
*)
