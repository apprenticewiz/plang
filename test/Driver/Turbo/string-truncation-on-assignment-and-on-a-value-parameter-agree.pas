(*
Tier 2 capstone: ShortString truncation on ORDINARY ASSIGNMENT and on a
VALUE PARAMETER, in the same program, to show both truncation paths agree
on the exact same clamped result.  Each already has its own single-feature
test (shortstring-assignment-truncates-instead-of-erroring.pas,
shortstring-value-parameters-copy-at-the-callees-declared-width.pas), but
neither compares its own answer against the other's for one shared source
value -- exactly the kind of cross-check most likely to catch two
DIFFERENT clamping implementations that happen to agree only on the cases
already tested individually.  'Wide' (capacity 20) and 'Narrow' (capacity
5) both start from the SAME nine-character literal; assignment truncates
it when stored into Narrow directly, and a value-parameter call passing
the same literal into a capacity-5 formal truncates it identically inside
the callee -- both land on 'Hello' (5 characters), not on two different
five-character prefixes or lengths, and the caller's own Wide is completely
untouched by the callee's copy (value semantics, not aliasing).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:HelloWorld 10
CHECK-NEXT:Hello 5
CHECK-NEXT:Hello 5
CHECK-NEXT:HelloWorld 10
*)

program truncation_paths_agree;
var
  Wide:   string[20];
  Narrow: string[5];

procedure TakeNarrow(n: string[5]);
begin
  writeln(n, ' ', Length(n));
end;

begin
  Wide := 'HelloWorld';
  writeln(Wide, ' ', Length(Wide));

  { truncation on ORDINARY ASSIGNMENT: Narrow's own capacity is 5 }
  Narrow := Wide;
  writeln(Narrow, ' ', Length(Narrow));

  { truncation on a VALUE PARAMETER: the actual (Wide, capacity 20, still
    holding all ten characters) is copied at the CALLEE's declared width }
  TakeNarrow(Wide);

  { the caller's own Wide is unaffected by the callee's copy }
  writeln(Wide, ' ', Length(Wide));
end.
