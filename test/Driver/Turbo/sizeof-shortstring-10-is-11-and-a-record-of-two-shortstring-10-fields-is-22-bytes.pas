(*
Tier 2 capstone: 'SizeOf(string[10]) = 11' (the declared capacity plus the
one-byte length prefix, no padding at all -- ShortString's whole struct is
PACKED (an i8 length byte immediately followed by N raw data bytes, no
gap), 1-byte aligned throughout, unlike EP's
string(N), which pads its 8-byte length header out to an 8-byte boundary),
and a RECORD containing two 'string[10]' fields is exactly 22 bytes, not
22 rounded up to some wider alignment -- because nothing inside a
ShortString is ever wider than a byte, the record's own alignment is 1,
so there is no tail padding to round up to either.  A sanity check against
a THIRD, differently-sized ShortString field in the same record confirms
this is genuine per-field byte-exact packing, not a coincidence of two
equal-sized fields lining up.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program shortstring_sizes;
type
  TTwo   = record
    a, b: string[10];
  end;
  TThree = record
    a, b: string[10];
    c: string[10];
  end;
var
  s: string[10];
begin
  writeln(SizeOf(s));
  writeln(SizeOf(TTwo));
  writeln(SizeOf(TThree));
end.
