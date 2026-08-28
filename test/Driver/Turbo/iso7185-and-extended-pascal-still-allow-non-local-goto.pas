(*
The critical non-regression check for -std=turbo rejecting a non-local goto
(checkGoto's Opts.turbo() gate, SemaStmt.cpp): ISO 7185 §6.8.1 and Extended
Pascal both genuinely allow a goto from a nested procedure to a label of an
enclosing block, abandoning every activation in between -- that is what
LabelGotoEngine's setjmp/longjmp-shaped machinery exists to run.  Neither
dialect gains the Turbo restriction just because Turbo grew one: the same
source, unmodified, still compiles AND still actually jumps (not merely
"compiles") under both.  Same source under both dialects; only -std changes.
See nonlocal-goto-is-rejected-under-turbo.pas (test/Sema/SemaGoto) for the
Turbo side of this contrast.
*)

(*
RUN: %plang -std=iso7185 %s -o %t.iso
RUN: %run %t.iso | FileCheck --strict-whitespace --match-full-lines %s

RUN: %plang -std=iso10206 %s -o %t.ep
RUN: %run %t.ep | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:inner
CHECK-NEXT:landed
*)

program p(output);
label 1;
procedure outer;
  procedure inner;
  begin writeln('inner'); goto 1 end;
begin inner; writeln('not reached in outer') end;
begin
  outer;
  writeln('not reached in main');
1:
  writeln('landed')
end.
