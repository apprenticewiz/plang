(*
Real Turbo Pascal's read(f, i) accepts three radix prefixes ISO 7185/EP's
reader never recognizes at all (a leading '$' or '&' or '%' is simply not a
number-shaped character to the ISO/EP scanner, so a token starting with one
of these would be a malformed-token error there, not a value) -- $ or 0x
for hexadecimal, & for octal, % for binary -- confirmed against `fpc -Mtp`:
$FF reads as 255, &17 as 15 (octal 17), %101 as 5 (binary 101), and 0x1A as
26.  No sign is recognized before a prefix (`-$FF` is itself a malformed
token, reported the same as any other -- checked directly against
`fpc -Mtp`), which is why decimal's own leading -/+ is left to strtoll
rather than folded into the same prefix-stripping step.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:255
CHECK-NEXT:15
CHECK-NEXT:5
CHECK-NEXT:26
*)

//--- test.pas
program p;
var i: Integer;
begin
  readln(i); writeln(i);
  readln(i); writeln(i);
  readln(i); writeln(i);
  readln(i); writeln(i)
end.

//--- stdin.txt
$FF
&17
%101
0x1A
