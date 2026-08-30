(*
Turbo Tier 4, Cluster A item 3: buildTUIContent's writeConst (Frontend.cpp)
used to write a Turbo typed constant (`identifier ':' type '=' value`,
ConstDef::Type set -- see that field's own comment) out as a PLAIN untyped
const, `const CB = 200;`, silently dropping the ': Byte'. Read back, Sema
gave the reloaded 'CB' plang's own default Integer type instead of the
Byte the defining unit actually declared -- a different width and
signedness (16-bit signed default Integer under -std=turbo vs. an 8-bit
unsigned Byte), so a program 'uses'ing the unit read back a WRONG VALUE for
every sized-integer typed constant whose value did not happen to also fit
the default type, not just a diagnostic or a dropped declaration.

This test compiles a unit exporting typed constants of Turbo's sized
integer types (Byte, Word, ShortInt, LongInt) standalone, deletes the
source, and proves a program built against nothing but the published .tui
and .o reads back the exact values the unit declared -- CW (50000) and CL
(2000000000) both overflow a default 16-bit Integer, so either one reading
back wrong proves the type was lost.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/typedconstunit.pas -o %t.dir/typedconstunit.o
RUN: FileCheck --check-prefix=TUI %s < %t.dir/typedconstunit.tui
RUN: rm %t.dir/typedconstunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/typedconstunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
TUI: const CB: Byte = 200;
TUI: const CW: Word = 50000;
TUI: const CS: ShortInt = - 100;
TUI: const CL: LongInt = 2000000000;
*)

(*
CHECK:200
CHECK-NEXT:50000
CHECK-NEXT:-100
CHECK-NEXT:2000000000
*)

//--- typedconstunit.pas
unit TypedConstUnit;

interface

const
  CB: Byte     = 200;
  CW: Word     = 50000;
  CS: ShortInt = -100;
  CL: LongInt  = 2000000000;

implementation

end.

//--- main.pas
program UsesTypedConstUnit;
uses TypedConstUnit;
begin
  Writeln(CB);
  Writeln(CW);
  Writeln(CS);
  Writeln(CL);
end.
