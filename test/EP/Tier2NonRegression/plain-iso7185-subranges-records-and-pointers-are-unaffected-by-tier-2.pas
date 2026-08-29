(*
Non-regression (Tier 2 capstone): a plain ISO 7185 program using ordinary
(unbounded, always-64-bit) subranges, records, and pointers still behaves
exactly as it always has.  Every Tier 2 feature -- the sized-integer
ladder's storage narrowing, PChar arithmetic, ShortString, and the rest --
is minted only when Opts.turbo() is true (TypeContext's own Turbo_ flag,
threaded through getSubrange/getInt/etc.; see docs/turbo.md's own repeated
"gated on the same Turbo_ flag" refrain throughout its Tier 2 sections), so
none of it should be reachable at all under the DEFAULT dialect.  This is
the lit-suite half of that claim's verification; the other half is a
direct '-emit-llvm' IR-text comparison against a pre-Tier-2 baseline build
(commit 0793458, the last commit of Tier 1), spot-checked over a sample of
this suite's own existing ISO 7185/Extended Pascal programs and reported
in this capstone's own PR description -- not repeated here as a permanent
test, since it depends on a second, historical compiler binary this suite
itself has no general mechanism for building.

A subrange (1..100, unaffected by Turbo's ch.19 narrowing: this is NOT
'-std=turbo', so it stays the wide, uniform storage ISO 7185 has always
used) inside a record, a record containing a pointer to another instance
of itself (an ordinary linked-list node), and 64-bit integer arithmetic
that would silently wrap at a Turbo sized-integer width but must NOT wrap
here (ISO 7185's own Integer is unbounded/64-bit, with no narrower rung to
fall into) are all exercised together.
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:50 100
CHECK-NEXT:1 2 3
CHECK-NEXT:9223372036854775807
*)

program p(output);
type
  grade = 1..100;
  node = ^cell;
  cell = record
    value: integer;
    next: node
  end;
var
  g1, g2: grade;
  head, cur: node;
  i, total, big: integer;
begin
  g1 := 50; g2 := 100;
  writeln(g1, ' ', g2);

  head := nil;
  for i := 3 downto 1 do begin
    new(cur);
    cur^.value := i;
    cur^.next := head;
    head := cur
  end;
  cur := head;
  total := 0;
  while cur <> nil do begin
    if total > 0 then write(' ');
    write(cur^.value);
    total := total + 1;
    cur := cur^.next
  end;
  writeln;

  { ISO 7185's own Integer is a full 64 bits, never narrowed the way
    Turbo's sized-integer ladder narrows a declared subrange -- this must
    still hold its full magnitude, not wrap at 16/32 bits the way a Turbo
    program using a narrower declared type could. }
  big := 9223372036854775807;
  writeln(big)
end.
