(*
Regression guard for the width threading in constBoundImpl (SemaType.cpp):
only a genuine TypeKind::Integer answers with a narrow Width/Signed pair.
Char's Type::Width is 8 but its ordinal range is 0..255 (unsigned) even
though Type::IsSigned defaults true on it -- ordinalRange (Type.h)
special-cases Char on its own terms rather than deriving its range from
Width/IsSigned the way an actual narrow Integer's is.  Running succ('a')
through the SAME narrowIntBounds(8, /*Signed=*/true) an Integer of that
width would use would wrongly reject it once its ordinal (97) crossed 127,
so this fold must leave Char alone entirely, the same as it always has.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:b
*)

program t;
const C = succ('a');
var ch: Char;
begin
  ch := C;
  writeln(ch)
end.
