(*
Issue #601: the assert arm in SemaStmt.cpp's checkCallStmt checked the
message argument against isCharStringType/String/VarString/Char but omitted
isShortStringLike, so an ordinary `string`/`string[N]` VARIABLE (as opposed
to a string literal, which parses as a VarString-kind constant) was wrongly
refused with "'string[255]' cannot be an argument of assert; it must be char
or string". `fpc -Mtp` 3.2.2 accepts a short-string variable here
(confirmed empirically), and so must plang.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: not %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 227: Assertion failed: boom
*)

program assert_short_string_message;
var s: string;
begin
  s := 'boom';
  Assert(false, s)
end.
