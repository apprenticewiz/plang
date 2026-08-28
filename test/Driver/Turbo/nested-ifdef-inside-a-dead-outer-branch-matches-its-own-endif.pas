(*
skipToNextConditionalMarker (lib/Lex/Directives.cpp) tracks nested
IFDEF/IFNDEF...ENDIF pairs found while skipping a dead branch with a plain
local depth counter, never CondStack itself -- see its own comment in
Scanner.h.  This is what keeps a nested ENDIF from being mistaken for the
outer, dead conditional's own ENDIF: OUTER is never defined, so the whole
block -- including its own nested INNER/ENDIF pair -- must be skipped as
one unit, leaving only the trailing writeln reachable.  A naive
implementation that stops raw-skipping at the FIRST ENDIF it sees, with no
depth counter, would stop at INNER's ENDIF instead and start scanning
'writeln('outer-live-2');' as if it were live -- wrong output, or (since
that line is itself inside what should still be a comment) a parse error.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
begin
  {$IFDEF OUTER}
  writeln('outer-live');
  {$IFDEF INNER}
  writeln('inner-live');
  {$ENDIF}
  writeln('outer-live-2');
  {$ENDIF}
  writeln('done')
end.

(*
CHECK:done
*)
