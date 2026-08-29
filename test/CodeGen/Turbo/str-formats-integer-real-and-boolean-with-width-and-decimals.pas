(*
System-unit string routines item: Str(x [: width [: decimals]], var s)
formats x the same way write(x [: width [: decimals]]) already does,
reusing the writestr capture machinery (BuiltinIO.cpp's emitBuiltinStr) --
so Str inherits write's own formatting rules exactly, width/decimals
included.  Also exercises truncation into a NARROW ShortString destination
(the same clamp every other ShortString-producing routine here already has):
formatting an Integer that overflows the destination's declared width wraps
via ordinary 2's-complement truncation (confirmed against `fpc -Mtp`;
123456 does not fit Turbo's 16-bit Integer, wraps to -7616, and Str's own
"-7616" is then truncated to the 3-capacity destination's first 3
characters, "-76") -- CGProcCall.cpp's sstrArgPtr has nothing to do with
this particular truncation: it is plang_writestr_end_sstr's own capacity
clamp (plang_io.cpp), the ShortString sibling of plang_writestr_end/
_end_fixed.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
  s3: string[3];
  i: integer;
  r: real;
  b: boolean;
begin
  i := 42;
  Str(i, s);
  writeln(s);

  Str(i:6, s);
  writeln(s);

  r := 3.14159;
  Str(r:0:2, s);
  writeln(s);

  Str(r:10:3, s);
  writeln(s);

  b := true;
  Str(b, s);
  writeln(s);

  i := 123456;
  Str(i, s3);
  writeln(s3, ' ', Length(s3));
end.

(*
CHECK:42
CHECK-NEXT:    42
CHECK-NEXT:3.14
CHECK-NEXT:     3.142
CHECK-NEXT:TRUE
CHECK-NEXT:-76 3
*)
