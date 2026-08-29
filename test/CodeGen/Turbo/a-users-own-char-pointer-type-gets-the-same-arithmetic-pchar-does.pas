(*
The load-bearing empirical finding behind this whole feature's design: real
`fpc -Mtp` (checked directly, fpc 3.2.2, multiple ways -- both the tp and
the objfpc compiler mode, and with the pointermath directive forced off and
the typed-pointers directive forced on, to rule either one out as the real
gate) grants pointer arithmetic, `p[i]` indexing, and zero-based-array
decay to ANY pointer whose pointee is Char --
`type MyCharPtr = ^Char; var p, q: MyCharPtr` -- not only to the identifier
`PChar`.  `^Byte` and `^Integer` pointer-aliases were checked too and do NOT
get this treatment (fpc: `Operation "+" not supported`), so the real rule is
structural on the pointee ("is Char"), not "any narrow pointee" and not "is
literally spelled PChar".

Sema's gate (isCharPointerType, include/plang/Sema/Type.h; used from
checkBinary/checkIndex/isAssignCompatible in SemaExpr.cpp) matches that:
identity to the PChar singleton (TypeContext::getPChar()) is NOT the check,
structurally "Pointer whose PointeeType is Char" is.  This file is the
positive proof; the ISO 7185/Extended Pascal negative side (the same
`^char` pointer-arithmetic getting refused once Opts.turbo() is false) is
test/Driver/Turbo/char-pointer-arithmetic-is-rejected-under-iso7185-and-extended-pascal.pas
and its indexing sibling -- Opts.turbo() is the only thing standing between
this file's behavior and ISO/EP's "=, <> only" (ISO §6.7.2.5), and it is
still checked at every one of these three call sites.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A
CHECK-NEXT:B
CHECK-NEXT:Z
*)

program p;
type
  MyCharPtr = ^Char;
var
  p, q: MyCharPtr;
  buf: array[0..3] of Char;
begin
  buf[0] := 'A';
  buf[1] := 'B';
  p := buf;         { decay: not the PChar name, still accepted }
  q := p + 1;         { arithmetic: not the PChar name, still accepted }
  writeln(p[0]);        { index as rvalue }
  writeln(q[0]);
  q[0] := 'Z';            { index as lvalue }
  writeln(buf[1]);          { q pointed one past buf[0], so this is buf[1] }
end.
