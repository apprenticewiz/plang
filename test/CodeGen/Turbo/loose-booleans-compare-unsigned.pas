(*
TypeContext::getLooseBoolean used to leave Type::IsSigned at its struct
default (true) instead of setting it false the way Type::makeBoolean sets
it for strict Boolean (see #463, "Make ordinalIsUnsigned consult
Type::IsSigned" -- landed on main while this feature was in flight and
rebased onto here).  CodegenImpl::ordinalIsUnsigned reads that field
directly to decide whether a '<'/'>' comparison is CreateICmpSLT or
CreateICmpULT: left signed, WordBool(40000) -- 0x9C40, negative as a signed
i16 -- would compare as though it were less than a small positive value
like 100, exactly backwards from what "any bit pattern, read as a plain
unsigned magnitude" (Type::IsLooseBool's own contract) promises.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:unsigned-correct: w1 is greater
*)

var
  w1, w2: WordBool;
  raw1: Word absolute w1;
  raw2: Word absolute w2;
begin
  w1 := false;
  w2 := false;
  raw1 := 40000; // top bit set: negative if ever read as a signed i16
  raw2 := 100;
  if w1 > w2 then writeln('unsigned-correct: w1 is greater')
  else writeln('WRONG: signed compare treated 40000 as negative');
end.
