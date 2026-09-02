(*
Issue #795's fix (see the sibling qword-literal-past-int64-max-issue-795.pas)
is deliberately narrow: a literal past Int64::max is now accepted only where
the destination can itself hold the full unsigned 64-bit range (QWord).
Assigning the exact same literal (UInt64::max) to a signed 64-bit
destination (Int64) must still be rejected -- there is no bit pattern that
would represent that value in a signed 64-bit variable, so silently
accepting it and reinterpreting the two's-complement pattern would assign
-1, not 18446744073709551615.  This is the same "integer literal ... is out
of range" diagnostic the parser used to give unconditionally for every
destination before this fix, now reported by
Sema::warnIfConstantOutOfRange once the destination type is known instead
of by the parser before it ever sees one.

The identical rejection applies to passing the same literal bare as a
value-parameter actual to a signed (or narrower) formal
(Sema::checkCallArgs' value-parameter arm, mirroring
Sema::warnIfConstantOutOfRange's own assignment-target check).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: integer literal '18446744073709551615' is out of range
CHECK: integer literal '18446744073709551615' is out of range
*)

program qwordLiteralRejectedForInt64;
var
  x: Int64;

procedure TakeInt64(v: Int64);
begin
  writeln(v);
end;

begin
  x := 18446744073709551615;
  writeln(x);
  TakeInt64(18446744073709551615);
end.
