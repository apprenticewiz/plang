(*
Issue #702: a string LITERAL ('HELLO') and `@buf` for a whole zero-based
char array were both rejected as PChar-parameter actuals -- "type mismatch:
expected PChar, got string" and "cannot assign '^array[0..15] of char' to
PChar" respectively -- even though the array itself (`p := buf`, no '@')
already decayed to PChar correctly (see
pchar-pointer-arithmetic-indexing-and-array-decay.pas), and real `fpc -Mtp`
accepts both forms outright.  Exercises Sema::isAssignCompatible's two new
arms (SemaExpr.cpp) plus StringCallMarshalling::emitCallArg's new
StringLitExpr-as-PChar-argument case.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:literal-vs-literal: 0
CHECK-NEXT:at-buf: 0
*)

program StrCompPCharDecay;
uses Strings;
var
  p: PChar;
  buf: array[0..15] of Char;
begin
  writeln('literal-vs-literal: ', StrComp('HELLO', 'HELLO'));
  StrCopy(buf, 'HI');
  p := @buf;
  writeln('at-buf: ', StrComp(p, buf));
end.
