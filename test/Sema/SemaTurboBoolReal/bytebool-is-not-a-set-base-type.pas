(*
Turbo's loose ByteBool/WordBool/LongBool (Type::IsLooseBool) have no 0-to-1
ordinal interval the way strict Boolean does -- see Type::IsLooseBool's own
comment in Sema/Type.h for the empirical fpc -Mtp trail -- so
checkSetBaseRange (SemaType.cpp) treats one as an unbounded base type, the
same as a bare Integer just above it in that switch, rather than the
always-fits two-value interval strict Boolean gets.  Checked against real fpc 3.2.2,
which refuses even the narrowest of the three ("illegal type declaration of
set elements") though ByteBool's own storage domain (0..255) would
technically fit the 256-element limit.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: set base type 'ByteBool' exceeds the 256-element limit on sets
*)

type
  SB = set of ByteBool;
begin
end.
