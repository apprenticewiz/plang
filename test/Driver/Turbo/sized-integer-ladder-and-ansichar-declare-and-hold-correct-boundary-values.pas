(*
Tier 2 foundation: ShortInt, Byte, SmallInt, Word, LongInt, Cardinal,
LongWord, Int64, QWord and AnsiChar are now real, declarable -std=turbo
type names (Sema::registerBuiltins), each bound to the width/signedness
TypeContext::getInt already knew how to mint (or, for AnsiChar, to the
existing Char type -- see that block's own comment for why it needs no new
Type).  This is not just "compiles": each variable is driven to both ends
of its declared range, so a width or signedness Sema/CodeGen got wrong
would show up as a wrong printed value, not merely a build failure.  QWord
is checked at 0 and at Int64's own max rather than its true 2^64-1
maximum: this compiler's integer-literal parsing (Parser::parseFactor's
std::from_chars, and Scanner::scanHexLiteral alongside it) is int64_t
end-to-end, so no literal above INT64_MAX can be written at all yet --
a separate, pre-existing limitation this change does not touch.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-128
CHECK-NEXT:127
CHECK-NEXT:0
CHECK-NEXT:255
CHECK-NEXT:-32768
CHECK-NEXT:32767
CHECK-NEXT:0
CHECK-NEXT:65535
CHECK-NEXT:-2147483648
CHECK-NEXT:2147483647
CHECK-NEXT:0
CHECK-NEXT:4294967295
CHECK-NEXT:0
CHECK-NEXT:4294967295
CHECK-NEXT:-9223372036854775807
CHECK-NEXT:9223372036854775807
CHECK-NEXT:0
CHECK-NEXT:9223372036854775807
CHECK-NEXT:A
CHECK-NEXT:255
*)

program p;
var
  si: ShortInt;
  by: Byte;
  sm: SmallInt;
  wd: Word;
  li: LongInt;
  cd: Cardinal;
  lw: LongWord;
  i6: Int64;
  qw: QWord;
  ac: AnsiChar;
begin
  si := -128;   writeln(si);
  si := 127;    writeln(si);
  by := 0;      writeln(by);
  by := 255;    writeln(by);
  sm := -32768; writeln(sm);
  sm := 32767;  writeln(sm);
  wd := 0;      writeln(wd);
  wd := 65535;  writeln(wd);
  li := -2147483648; writeln(li);
  li := 2147483647;  writeln(li);
  cd := 0;           writeln(cd);
  cd := 4294967295;  writeln(cd);
  lw := 0;           writeln(lw);
  lw := 4294967295;  writeln(lw);
  i6 := -9223372036854775807; writeln(i6);
  i6 := 9223372036854775807;  writeln(i6);
  qw := 0;                    writeln(qw);
  qw := 9223372036854775807;  writeln(qw);
  ac := 'A';      writeln(ac);
  ac := chr(255); writeln(ord(ac))
end.
