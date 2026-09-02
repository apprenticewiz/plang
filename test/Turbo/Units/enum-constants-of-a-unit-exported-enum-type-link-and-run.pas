(*
Issue #695: a unit's interface can export an enumerated TYPE, and ISO
§6.4.2.3 says the identifiers of an enumerated type denote constants of it
-- so each of its enumerators is exported right along with the type, the
same way an ordinary interface constant is.  registerUsedUnitConsts
(CodeGenProcs.cpp) folded every plain interface constant into a compile-time
value for an importer, but never called registerEnumValues over the
interface's own Types, so an importer's reference to one of a used unit's
enum constants fell through to the generic imported-global fallback and
looked for storage (pasg_<Enumerator>) that was never emitted anywhere --
an enum constant is always a compile-time-folded ordinal, never a global at
all.  Exercises the enumerator both in an assignment and in an expression,
on both the .tui-published path (compiled here through a real -c) and
confirms qualification still resolves the plain name correctly when the
type itself was imported unqualified.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/serb.pas -o %t.dir/serb.o
RUN: rm %t.dir/serb.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/prog.pas %t.dir/serb.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Blue
CHECK-NEXT:ok
*)

//--- serb.pas
unit SerB;

interface

type
  TEnum = (Red, Green, Blue);

implementation

end.

//--- prog.pas
program TestCase;

uses SerB;

var
  E: TEnum;

begin
  E := Blue;
  case E of
    Red:   writeln('Red');
    Green: writeln('Green');
    Blue:  writeln('Blue');
  end;
  if (E = Blue) and (Ord(Blue) = 2) then
    writeln('ok')
  else
    writeln('fail');
end.
