(*
TypeContext::getInt interns every integer Type by width and signedness
alone, and Type::makeInteger hardcodes Name = "integer" -- so before this
Tier 2 foundation, that hardcoded "integer" was also the only name ANY
diagnostic about an integer-kind type could ever show, Turbo's own 16-bit
Integer included.  A diagnostic naming a `Word` variable's type as
"integer" would have been technically not wrong (Word IS an integer kind)
but uselessly vague exactly where the sized ladder makes the width the
whole point of the type name.

getInt now names each freshly-minted width/signedness pair from a fixed
Bits/Signed -> name table (ShortInt/Byte/Word/LongInt/Cardinal/Int64/QWord)
UNLESS the pair is the dialect's own unqualified `integer`
(DefaultIntWidth_, signed), which keeps makeInteger's plain "integer" so
every diagnostic about plain Integer (ISO 7185, Extended Pascal, and
Turbo's own Integer) reads exactly as it always has -- see `i` below and
negative-integer-arithmetic-is-sign-extended-not-zero-extended.pas /
and-on-integer-operands-is-a-type-error-under-iso7185-and-extended-pascal.pas,
neither of which this change may alter.

SmallInt (16, signed) and LongWord (32, unsigned) are the two ladder rungs
that are literally the same cached Type object as Integer and Cardinal
respectively (see
smallint-and-integer-are-literally-the-same-type-and-so-are-cardinal-and-longword.pas):
a diagnostic about one necessarily shows the other's name.  That is
correct, not approximate -- the two spellings name one identical type --
and `sm`/`lw` below lock in exactly which name each collision shows,
so a future change to getInt's naming table changes this test rather
than silently changing what users see.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign 'boolean' to variable of type 'Byte'
CHECK: cannot assign 'boolean' to variable of type 'Word'
CHECK: cannot assign 'boolean' to variable of type 'LongInt'
CHECK: cannot assign 'boolean' to variable of type 'Cardinal'
CHECK: cannot assign 'boolean' to variable of type 'Int64'
CHECK: cannot assign 'boolean' to variable of type 'QWord'
CHECK: cannot assign 'boolean' to variable of type 'integer'
CHECK: cannot assign 'boolean' to variable of type 'Cardinal'
CHECK: cannot assign 'boolean' to variable of type 'integer'
*)

program p;
var
  by: Byte;
  wd: Word;
  li: LongInt;
  cd: Cardinal;
  i6: Int64;
  qw: QWord;
  sm: SmallInt;  (* collides with Integer: shown as 'integer' *)
  lw: LongWord;  (* collides with Cardinal: shown as 'Cardinal' *)
  i:  Integer;   (* unchanged regression check: still 'integer' *)
begin
  by := true;
  wd := true;
  li := true;
  cd := true;
  i6 := true;
  qw := true;
  sm := true;
  lw := true;
  i  := true;
end.
