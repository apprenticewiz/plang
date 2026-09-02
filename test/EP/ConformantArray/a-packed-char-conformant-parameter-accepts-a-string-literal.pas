(*
Issue #687: ISO 7185 §6.4.3.2 makes a string literal's type
`packed array[1..n] of char`, which §6.6.3.6.2 lets conform to a packed
conformant-array-of-char parameter -- the canonical idiom `w('hi there')`
for a `w(s: packed array[l..u: integer] of char)`.  isConformable used to
require Actual.Kind == Array (a real array VARIABLE), which a literal's
own TypeKind::String never is, and rejected every literal actual outright.

RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi there
CHECK-NEXT:8
*)

program p(output);
procedure w(s: packed array[l..u: integer] of char);
var k: integer;
begin
  for k := l to u do write(s[k]);
  writeln;
  writeln(u - l + 1)
end;
begin
  w('hi there')
end.
