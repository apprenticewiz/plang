(*
Tier 2 capstone: a typed constant surviving two separate calls to the SAME
procedure, combined with 'absolute' aliasing a Byte directly over that same
record's first field, in one program.  Individually, both already have
dedicated single-feature coverage
(test/CodeGen/Turbo/typed-constant-persists-across-calls.pas,
test/CodeGen/Turbo/absolute-overlays-shared-storage.pas); this exercises
them together in one procedure's local-declaration list, since a typed
constant's own static-storage GlobalVariable and an 'absolute'-overlaid
variable's shared-pointer defVar hook into CodeGenProcs.cpp's local-
declaration machinery in different, easily-conflicting ways (one adds NEW
static storage nothing else points to, the other adds a symbol with NO
storage of its own, aliasing someone else's) -- nothing in either
feature's own test proves the two coexist correctly declared side by side.

Pair is called three times.  'hits' (a typed constant) counts the calls --
proof it is genuinely static storage, not a fresh stack slot, is that it
reads 1, 2, 3 rather than 1, 1, 1.  'ov' (declared 'absolute pr', a Byte
aliasing the record's FIRST field) is written on calls 1 and 2 and left
alone on call 3 -- proof it genuinely shares pr's own storage, not a copy,
is that pr.first, read through the ORDINARY field-access path, reflects
ov's write in the SAME call it happens, and call 3's read (with no write of
its own) still sees call 2's value rather than some freshly-initialized 0.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hits=1 first=65
CHECK-NEXT:hits=2 first=90
CHECK-NEXT:hits=3 first=90
*)

program typed_const_and_absolute_together;
type
  TPair = record
    first, second: Byte;
  end;
var
  pr: TPair;

procedure Pair;
const
  hits: Integer = 0;
var
  ov: Byte absolute pr;
begin
  hits := hits + 1;
  case hits of
    1: ov := 65;
    2: ov := 90;
  end;
  writeln('hits=', hits, ' first=', pr.first);
end;

begin
  Pair;
  Pair;
  Pair;
end.
