(*
Issue #170, -save-temps side: unlike the plain-build case (see
flattened-stem-collision-does-not-corrupt-either-extra-object.pas), a
-save-temps build keeps each extra file's .o (and .ll) as a visible,
human-inspectable artifact in the cwd rather than a throwaway temp file, so
the flattenedStem collision -- "unitA/b_c.pas" and "unitA_b/c.pas" both
flatten to "unitA_b_c" -- has to be resolved by disambiguating the name
itself instead of just sidestepping it with an OS-assigned one.  The second
file to claim an already-planned name now gets a numeric suffix
("unitA_b_c~2.o"/".ll") rather than silently overwriting the first file's
kept-for-inspection object *and* IR.

The .ll half of this matters on its own: the IR file name is derived from
the *already-disambiguated* .o name (Driver.cpp's "Choose IR file" step),
not recomputed independently from flattenedStem(inputFile) a second time --
otherwise the .o collision would be fixed while the .ll one silently
remained, since llc's own duplicate-symbol check that catches a .o
collision has no equivalent for two unrelated .ll files.
*)

(*
RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 -save-temps -I. main.pas unitA/b_c.pas unitA_b/c.pas -o prog
RUN: test -e %t.dir/unitA_b_c.o
RUN: test -e %t.dir/unitA_b_c.ll
RUN: test -e %t.dir/unitA_b_c~2.o
RUN: test -e %t.dir/unitA_b_c~2.ll
RUN: FileCheck --check-prefix=MOD1 %s < %t.dir/unitA_b_c.ll
RUN: FileCheck --check-prefix=MOD2 %s < %t.dir/unitA_b_c~2.ll
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
MOD1: pas_mod1$F1
MOD2: pas_mod2$F2
CHECK:111 222
*)

//--- unitA/b_c.pas
module Mod1;
function F1: integer;
begin F1 := 111 end;
end.

//--- unitA_b/c.pas
module Mod2;
function F2: integer;
begin F2 := 222 end;
end.

//--- main.pas
program p;
import Mod1; Mod2;
begin writeln(F1, ' ', F2) end.
