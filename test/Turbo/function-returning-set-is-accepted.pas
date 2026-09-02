(*
Issue #787: `-std=turbo` wrongly rejected a function returning a `set` type
("error: a function result must be a simple or pointer type"), even though
this is ordinary, idiomatic Turbo Pascal 7 -- confirmed against a local
`fpc -Mtp` install, which accepts and runs the form below.  `checkProcSignature`
(lib/Sema/Sema.cpp) already whitelisted Record and non-open Array results for
Turbo (issue #585); Set was simply missing from that whitelist.  A set is
lowered to a plain integer bitmask (SetOps::setTy), not an LLVM struct, so
unlike Record/Array it needs none of CodeGen's by-value struct-return spill
treatment -- it round-trips as an ordinary scalar return value.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 in result: TRUE
CHECK-NEXT:0 in result: FALSE
CHECK-NEXT:0 in s3: FALSE
*)

program t;
type
  TSmallSet = set of 0..7;

function Combine(A, B: TSmallSet): TSmallSet;
begin
  Combine := A + B;
end;

var
  S1, S2, S3: TSmallSet;
begin
  S1 := [1, 2, 3];
  S2 := [3, 4, 5];
  S3 := Combine(S1, S2);
  if 4 in Combine(S1, S2) then
    Writeln('4 in result: TRUE')
  else
    Writeln('4 in result: FALSE');
  if 0 in Combine(S1, S2) then
    Writeln('0 in result: TRUE')
  else
    Writeln('0 in result: FALSE');
  if 0 in S3 then
    Writeln('0 in s3: TRUE')
  else
    Writeln('0 in s3: FALSE');
end.
