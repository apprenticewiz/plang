(*
Turbo Tier 5 capstone (Cluster C item 9): a real bug found WHILE WRITING
this capstone's own roster scenario (this directory's main test, which
needed to ask "which concrete type does this ancestor-typed pointer
actually point to" -- the textbook use for TypeOf/ClassType-style runtime
type identification, and the one item 7's own precedent tests never
actually tried) -- confirmed empirically against a local `fpc -Mtp` 3.2.2
install before this file was written down, the same discipline every other
empirical claim in this tier's own tests already follows.

CGFuncCall.cpp's own TypeOf lowering (search "Turbo Tier 5, Cluster A item
7: TypeOf(x)") is explicit that this is deliberate, not an oversight:
"this answers a purely STATIC question (which VMT does x's declared TYPE
have -- never "what does x hold at run time"... Args[0] is never
evaluated for its value, only asked for its type" -- and picks the VMT
from 'Args[0]->ResolvedType', Sema's STATIC type for the argument
expression. That is indistinguishable from a correct, DYNAMIC answer for
every case item 7's own tests and this directory's own
typeof-distinguishes-directly-typed-instances-across-a-three-level-
hierarchy.pas exercise, because in all of them a variable's own static
type and its own dynamic type are the same thing. It stops agreeing with
real Turbo Pascal / `fpc -Mtp` field practice the moment the two diverge:
a pointer whose declared pointee type is an ANCESTOR, holding the address
of a DESCENDANT instance -- exactly the shape virtual dispatch itself
(P^.Describe, already proven correct by item 5 and reused throughout this
whole tier) is built to handle correctly, but TypeOf is not.

Below, P: PEmployee actually points at a live TExecutive (P^.Describe
dispatches to TExecutive's own override correctly, first line of output
-- proving the object's real runtime type well before TypeOf is asked
about it at all). Real `fpc -Mtp` reads TExecutive's own instance through
its '_vptr' field, exactly like virtual dispatch itself does, and answers
"yes, this is a TExecutive, no, it is not a plain TEmployee" -- confirmed
directly below via a side-by-side mode-tp-compatible build of this
identical program. plang answers the opposite of both, in both
directions, purely from P^'s STATIC type (TEmployee, P's own declared
pointee type):

    $ fpc -Mtp typeof_gap_repro.pas && ./typeof_gap_repro
    executive
    P^ correctly identified as TExecutive
    P^ correctly NOT identified as plain TEmployee

    $ plang -std=turbo typeof_gap_repro.pas -o t && ./t
    executive
    P^ NOT identified as TExecutive
    P^ (incorrectly) identified as plain TEmployee

Not fixed here, for the same reason the two runtime gaps Tier 3's own
capstone found and pinned instead of fixing were not fixed there (see
that tier's own former
read-of-malformed-numeric-input-does-not-yet-honor-i-minus-known-gap.pas /
reset-does-not-yet-open-read-write-known-gap.pas, both since fixed and
renamed -- c0a1eac): this item's own scope is a test corpus plus
documentation, no new compiler/runtime behavior. A real fix means
CGFuncCall.cpp's TypeOf lowering loading the operand's own '_vptr' field
at run time (the same GEP-and-load getOrCreateVmt's own callers already
use for a virtual call) instead of resolving 'Args[0]->ResolvedType'
statically -- genuine CodeGen work, not a test-only change. Pinned to
plang's CURRENT (incorrect) behavior so a future fix shows up as an
intentional, reviewed test change, and cross-referenced from
docs/turbo.md's own "Documented deviations" section.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program TypeOfGap;

type
  PEmployee = ^TEmployee;
  TEmployee = object
    constructor Init;
    procedure Describe; virtual;
  end;
  TExecutive = object(TEmployee)
    procedure Describe; virtual;
  end;

constructor TEmployee.Init; begin end;
procedure TEmployee.Describe; begin writeln('employee'); end;
procedure TExecutive.Describe; begin writeln('executive'); end;

var
  X: TExecutive;
  P: PEmployee;
begin
  X.Init;
  P := @X;
  P^.Describe;   { correct: dispatches to TExecutive.Describe, proving P really points at a TExecutive }

  if TypeOf(P^) = TypeOf(TExecutive) then
    writeln('P^ correctly identified as TExecutive')
  else
    writeln('P^ NOT identified as TExecutive');   { plang takes this branch -- the known gap }

  if TypeOf(P^) = TypeOf(TEmployee) then
    writeln('P^ (incorrectly) identified as plain TEmployee')   { plang takes this branch -- the known gap }
  else
    writeln('P^ correctly NOT identified as plain TEmployee');
end.

(*
CHECK:executive
CHECK-NEXT:P^ NOT identified as TExecutive
CHECK-NEXT:P^ (incorrectly) identified as plain TEmployee
*)
