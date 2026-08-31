(*
Turbo Tier 4, Cluster C item 7's shipped `Strings` unit: StrPCopy/StrPLCopy,
which convert a Pascal ShortString to a null-terminated PChar.  Both take
their `Source` as `var string` rather than real Borland/FPC's `const string`
-- a forced deviation, not stylistic; see share/plang/units/Strings.pas' own
header comment for exactly why (a `string` argument passed BY VALUE across
this project's own extern-declaration boundary lowers into a shape no
hand-written C++ signature could reproduce, confirmed by reading the emitted
LLVM IR before this unit was written). The cost is visible right here: the
source has to be a real string VARIABLE (S below), not a literal, unlike
real Borland/FPC's own `const`.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrPCopy=World
CHECK-NEXT:StrPLCopy(4)=TooL
*)

program StrPCopyPLCopy;
uses Strings;
var
  Buf: array[0..40] of Char;
  PBuf: PChar;
  S: string;
begin
  PBuf := Buf;
  S := 'World';
  StrPCopy(Buf, S);
  Writeln('StrPCopy=', PBuf);

  S := 'TooLongToFit';
  StrPLCopy(Buf, S, 4);
  Writeln('StrPLCopy(4)=', PBuf);
end.
