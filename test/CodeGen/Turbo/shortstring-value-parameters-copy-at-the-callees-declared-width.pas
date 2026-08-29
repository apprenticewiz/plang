(*
Turbo string[N] semantics item, concrete work 5: a value parameter is
copied at the CALLEE's declared capacity, not the caller's -- passing a
string[20] actual to a `procedure p(s: string[5])` formal builds a
5-capacity temporary and truncates into it (plang_sstr_assign), and the
callee can never see or hold more than 5 characters' worth.  Also the
regression coverage for this item's single most concrete confirmed-live
bug: StringCallMarshalling::emitCallArg used to dispatch which runtime
function family to marshal a struct-shaped argument through by reading the
raw LLVM struct SHAPE alone (a 2-element struct whose 2nd element is an
array) rather than paramTy->isPacked() -- indistinguishable from EP's own
"i64 length, N x i8 bytes" shape by that test alone, so a ShortString
actual was silently marshaled with EP's eight-byte length-header
geometry.  (Verified
during development: reverting the isPacked() check back to an unconditional
emitStrStore call made this exact program abort with "plang runtime: string
of length 6 assigned to a string(5)" instead of printing the expected
output below -- plang_str_assign misreading a one-byte ShortString header
as if it were an eight-byte one.)

Confirms the caller's own copy is untouched by the callee's truncation
(value semantics), and that the callee's OWN further mutation of its local
copy (concatenation) doesn't touch the caller's storage either.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
procedure grow(s: string[5]);
begin
  writeln('callee before: ', s, ' len=', ord(s[0]));
  s := s + 'ZZZZZ';
  writeln('callee after: ', s, ' len=', ord(s[0]));
end;
var caller: string[20];
begin
  caller := 'abcdefghij';
  grow(caller);
  writeln('caller: ', caller, ' len=', ord(caller[0]));
end.

(*
CHECK:callee before: abcde len=5
CHECK-NEXT:callee after: abcde len=5
CHECK-NEXT:caller: abcdefghij len=10
*)
