(*
Issue #170: flattenedStem disambiguates two extra files sharing a basename
in different directories (issue #20, see same-basename-different-
directories-do-not-collide.pas) by folding the whole relative path into one
component, turning every '/' into '_'.  That folding is not injective,
though: "unitA/b_c.pas" and "unitA_b/c.pas" both flatten to "unitA_b_c" --
two different files whose *default* object path was the same, the second
compile silently overwriting the first's .o on disk before the link step
ever ran, which then handed ld.lld the same object twice (once under each
extra file's own name in Opts.linkerArgs, both resolving to one path) --
reproduced directly against pre-fix origin/main as a "duplicate symbol"
link failure.

Fixed by giving each extra file's object a real, OS-named unique temp file
instead of a flattenedStem-derived cwd path whenever -save-temps is not
given (see extra-file-objects-do-not-litter-the-cwd-without-save-temps.pas
for that same change's other half, issue #279): the collision cannot occur
at all when the name comes from the OS rather than from folding the input
path.  This test's real assertion is that the *link* and the *run* both
succeed and each module's own function returns its own value -- proof
neither extra object silently clobbered or replaced the other.
*)

(*
RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 -I. main.pas unitA/b_c.pas unitA_b/c.pas -o prog
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
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
