(*
EP §6.1.3: a Pascal identifier is case-insensitive, and a module name is no
exception -- but writePMIFiles used to name the file it published after the
module's OWN declaration spelling (ReviewCaseModule.pmi) while
Sema::processImports looked the file up under the IMPORT clause's spelling
(reviewcasemodule.pmi).  Those happen to agree only when both sides are
written in the same case; a case-sensitive filesystem does not extend the
language's own case-insensitivity promise to whichever case the two sides
picked independently, so a legal, single-file-compiled program broke the
moment it crossed a -c boundary.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

//--- mod.pas
module ReviewCaseModule;
function ReviewCaseVal: integer;
begin ReviewCaseVal := 5 end;
end.

//--- prog.pas
program p(output);
import reviewcasemodule;
begin writeln(ReviewCaseVal) end.
