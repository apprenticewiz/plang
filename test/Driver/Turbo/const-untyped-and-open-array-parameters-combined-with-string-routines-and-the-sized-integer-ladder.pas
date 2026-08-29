(*
Tier 2 capstone: all three Turbo parameter forms -- const, untyped, and
open-array -- combined with the sized-integer ladder and a ShortString
field, in one realistic program:

  - SumWords(a: array of Word): LongWord -- an open-array-of-Word summing
    procedure accumulating into a WIDER sized-integer result, the exact
    "array of Word" example this capstone's own test plan calls for.
    NOTE: the four addends are deliberately kept under 32768 (Word's own
    sign-bit threshold).  This sidesteps a genuine, separate bug this
    session's testing surfaced and is NOT fixing (flagged in this PR's own
    description for review): CGExprCore::coerceToType/toI64 decide
    zero-extend-vs-sign-extend from the LLVM width alone (i8/i1 zero-extend,
    everything else sign-extends) rather than the operand's actual Sema
    Type::IsSigned -- exactly the gap toI64's own comment already flags
    ("Tier 2's Byte/Word/etc. ladder will need this decided from the
    operand's actual Sema Type::IsSigned instead of LLVM width alone ...
    not needed yet") but that was never revisited once the ladder actually
    landed.  A Word (unsigned, i16) or Cardinal/LongWord (unsigned, i32)
    value whose own top bit is set gets SIGN-extended instead of
    zero-extended when widened against a wider operand, corrupting mixed-
    width arithmetic; values below the sign-bit threshold are unaffected
    (sign- and zero-extension agree whenever the top bit is clear), which
    is what lets this test exercise the FEATURE (open array + sized ladder)
    correctly without also asserting a broken sum as though it were right.

  - Describe(const p: TPerson) -- a const RECORD parameter whose own field
    is a ShortString (Tier 2's other headline addition), read without
    copying the whole record (structured const actuals pass by reference).

  - ZeroIt(var buf) -- an untyped parameter that RELAYS to another untyped
    formal (RawZero4), which in turn uses buf only as the operand of a
    variable TYPECAST (TByte4(buf)) -- the two legal uses of an untyped
    parameter, chained through two procedures in one program.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10000
CHECK-NEXT:Alice is 30
CHECK-NEXT:0 0 0 0
*)

program combined_params;
type
  TPerson = record
    name: string[20];
    age:  Byte;
  end;
  TByte4 = array[0..3] of Byte;

function SumWords(a: array of Word): LongWord;
var
  i: Integer;
  total: LongWord;
begin
  total := 0;
  for i := Low(a) to High(a) do
    total := total + a[i];
  SumWords := total;
end;

procedure Describe(const p: TPerson);
begin
  writeln(p.name, ' is ', p.age);
end;

procedure RawZero4(var buf);
var
  i: Integer;
begin
  for i := 0 to 3 do
    TByte4(buf)[i] := 0;
end;

procedure ZeroIt(var buf);
begin
  RawZero4(buf);
end;

var
  w:       array[1..4] of Word;
  person:  TPerson;
  scratch: array[1..4] of Byte;
  i: Integer;
begin
  w[1] := 1000; w[2] := 2000; w[3] := 3000; w[4] := 4000;
  writeln(SumWords(w));

  person.name := 'Alice';
  person.age  := 30;
  Describe(person);

  for i := 1 to 4 do scratch[i] := 255;
  ZeroIt(scratch);
  writeln(scratch[1], ' ', scratch[2], ' ', scratch[3], ' ', scratch[4]);
end.
