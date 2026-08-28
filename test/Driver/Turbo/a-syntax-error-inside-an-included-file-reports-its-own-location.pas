(*
A diagnostic for something wrong inside an included file must name that
file, at its own line and column -- not main.pas, and not the line the
{$I} directive itself sits on.  This falls out of SourceManager's own
design (each buffer -- main.pas and broken.inc alike -- gets its own
contiguous stretch of the coordinate space and its own Name; see
SourceManager.h's header comment) plus openInclude switching the Scanner's
FID/Text/Pos onto broken.inc's own buffer for as long as it is being read:
every locAt() call made while scanning broken.inc's tokens necessarily
resolves through broken.inc's own FID, so getPresumedLoc reports broken.inc
by construction, not by any special-casing in the diagnostic path itself.
*)

(*
RUN: split-file %s %t.dir
RUN: not %plang -std=turbo %t.dir/main.pas -o %t.dir/prog > %t.dir/out.txt 2>&1
RUN: FileCheck %s < %t.dir/out.txt
*)

(*
CHECK: broken.inc:2:{{[0-9]+}}: error:
CHECK-NOT: main.pas:{{[0-9]+}}:{{[0-9]+}}: error:
*)

//--- broken.inc
  writeln('line one is fine');
  this is not valid pascal +++;

//--- main.pas
program brokenmain;
begin
{$I broken.inc}
end.
