(*
A bare `string` (no explicit `[N]`) under -std=turbo is real TP7/FPC field
practice for "ShortString at the default capacity" -- a `var s: string;`
declaration compiled under FPC's own Turbo-compatibility mode directive (-Mtp
at the command line, or its in-source equivalent) on a local install gives
SizeOf(s) = 256 (255 data bytes plus the one-byte length prefix), confirmed
empirically, not EP's unbounded String and not a rejection.  Sema::resolveNamedUnrestricted now
resolves it the same way `string[255]` already does (Ctx_.getShortString),
rather than always taking the EP-only err_ep_type branch every bare `string`
outside extendedPascal() used to.  Discovered as a blocking gap while
testing the routines in this same directory: every one of them needs a
ShortString destination, and idiomatic Turbo code almost always writes a
bare `string` rather than spelling out `string[255]`.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
begin
  writeln(SizeOf(s));
  s := 'hello world, this is a bare turbo string';
  writeln(s);
  writeln(Length(s));
  writeln(Pos('world', s));
end.

(*
CHECK:256
CHECK-NEXT:hello world, this is a bare turbo string
CHECK-NEXT:40
CHECK-NEXT:7
*)
