(*
Issue #651: skipToNextConditionalMarker (Directives.cpp) errored at the
CURRENT buffer's own end instead of popping an open {$I file}/{$INCLUDE
file} and continuing the skip in the parent buffer -- unlike next()'s own
ordinary EOF handling (Scanner.cpp), which already does exactly that (see
Scanner.h's own promise that {$I file} splices the included text in "as
if" pasted at the directive itself). A dead branch opened by an {$IFDEF}
inside an included file, whose matching {$ENDIF} is back in the includer
past the {$I} that opened it, used to report a spurious "no matching
{$ENDIF}" at the included buffer's own end AND a spurious "ENDIF with no
matching {$IFDEF}" back in the includer (the includer's real {$ENDIF},
now orphaned since the dead-skip never got back there to consume it). Live
branches already straddled this boundary fine -- the dead-skip path was
the one hole. Fixed by calling popInclude() at skipToNextConditionalMarker's
own EOF check, the same way next()'s does, before giving up.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang_ir -dump-tokens -std=turbo %t.dir/main.pas | FileCheck %s
*)

(*
CHECK: Begin "begin"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Identifier "writeln"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: LeftParen "("
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: StringLit "ok"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: RightParen ")"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: End "end"
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Dot "."
CHECK-NEXT: {{[0-9]+}}:{{[0-9]+}}: Eof
*)

//--- inc.pas
{$IFDEF NEVERDEFINED}

//--- main.pas
begin
  {$I inc.pas}
  writeln('unreachable')
  {$ENDIF}
  writeln('ok')
end.
