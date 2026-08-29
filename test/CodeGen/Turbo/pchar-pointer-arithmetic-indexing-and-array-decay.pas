(*
PChar (issue: PChar/PAnsiChar pointer arithmetic) end-to-end: pointer
arithmetic (p + n, p1 - p2), p[i] indexing as BOTH an rvalue and an lvalue,
and zero-based char-array-to-PChar decay (`p := buf` with no `@`, buf's
index type starting at 0).

Every one of these is gated in Sema on Ctx_.getPChar()-shaped pointers --
see isCharPointerType (include/plang/Sema/Type.h) -- and Opts.turbo(), and
verified empirically against a real `fpc -Mtp` (fpc 3.2.2) running the
identical program, whose output matches this file's CHECK lines exactly.

PAnsiChar is exercised too (the last two lines): it names the exact same
TypeContext singleton PChar does, not a merely-compatible one, so mixing
the two spellings on either side of an assignment or comparison is nothing
special to Sema -- pa here is declared PAnsiChar and freely takes what p
(PChar) computed.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:H
CHECK-NEXT:i
CHECK-NEXT:X
CHECK-NEXT:1
CHECK-NEXT:i
*)

program p;
var
  p, q: PChar;
  pa: PAnsiChar;
  buf: array[0..9] of Char;
  n: Integer;
begin
  buf[0] := 'H';
  buf[1] := 'i';
  p := buf;              { zero-based array decay -- no '@' needed }
  q := p + 1;             { pointer + integer }
  writeln(p[0]);           { index as rvalue }
  writeln(q[0]);
  p[0] := 'X';              { index as lvalue -- writes through the pointer }
  writeln(buf[0]);           { ... and buf[0] sees it: p really points at buf }
  n := q - p;                 { pointer - pointer -> element count }
  writeln(n);
  pa := q;                     { PAnsiChar <- PChar: the same type, not just compatible }
  writeln(pa[0]);
end.
