(*
Turbo Tier 4 capstone (integration): separate compilation proven at a
LARGER scale than any single Cluster A PR's own test -- each Cluster A/B PR
proved its own narrow claim with exactly two units (a shipped unit or a
program on one side, one freshly-written unit on the other); nothing in the
suite so far links THREE independently, separately-compiled units together
into one program.

Three real units, each doing genuine work, each compiled entirely on its
own via `plang -std=turbo -c` (a fresh process per unit, so nothing carries
sema/codegen state between them), then their .pas sources are DELETED
before the final program compile -- the same "strongest possible proof"
pattern a-unit-compiled-standalone-can-be-used-by-a-program-that-never-
sees-its-source.pas already established for one unit, extended here to
three linked together: TrimUnit strips leading/trailing spaces, CaseUnit
upper-cases, ReverseUnit reverses -- a real little text pipeline, not three
units that each just return a constant.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/trimunit.pas -o %t.dir/trimunit.o
RUN: %plang -std=turbo -c %t.dir/caseunit.pas -o %t.dir/caseunit.o
RUN: %plang -std=turbo -c %t.dir/reverseunit.pas -o %t.dir/reverseunit.o
RUN: rm %t.dir/trimunit.pas %t.dir/caseunit.pas %t.dir/reverseunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/trimunit.o %t.dir/caseunit.o %t.dir/reverseunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:trimmed=[hello world]
CHECK-NEXT:upper=[HELLO WORLD]
CHECK-NEXT:reversed=[DLROW OLLEH]
*)

//--- trimunit.pas
unit TrimUnit;

interface

function Trim(const S: string): string;

implementation

function Trim(const S: string): string;
var
  I, J: Integer;
begin
  I := 1;
  while (I <= Length(S)) and (S[I] = ' ') do I := I + 1;
  J := Length(S);
  while (J >= I) and (S[J] = ' ') do J := J - 1;
  if J >= I then
    Trim := Copy(S, I, J - I + 1)
  else
    Trim := '';
end;

end.

//--- caseunit.pas
unit CaseUnit;

interface

function UpCase2(const S: string): string;

implementation

function UpCase2(const S: string): string;
var
  I: Integer;
  R: string;
  C: Char;
begin
  R := '';
  for I := 1 to Length(S) do
  begin
    C := S[I];
    if (C >= 'a') and (C <= 'z') then
      C := Chr(Ord(C) - Ord('a') + Ord('A'));
    R := R + C;
  end;
  UpCase2 := R;
end;

end.

//--- reverseunit.pas
unit ReverseUnit;

interface

function ReverseStr(const S: string): string;

implementation

function ReverseStr(const S: string): string;
var
  I: Integer;
  R: string;
begin
  R := '';
  for I := Length(S) downto 1 do
    R := R + S[I];
  ReverseStr := R;
end;

end.

//--- main.pas
program StringPipeline;
uses TrimUnit, CaseUnit, ReverseUnit;
var
  Raw, Trimmed, Uppered, Reversed: string;
begin
  Raw := '  hello world  ';
  Trimmed := Trim(Raw);
  Writeln('trimmed=[', Trimmed, ']');
  Uppered := UpCase2(Trimmed);
  Writeln('upper=[', Uppered, ']');
  Reversed := ReverseStr(Uppered);
  Writeln('reversed=[', Reversed, ']');
end.
