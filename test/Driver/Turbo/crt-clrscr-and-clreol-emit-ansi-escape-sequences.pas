(*
Turbo Tier 4, Cluster C item 5: ClrScr/ClrEol, over a full (default 80x25)
window, are the fast-path ESC[2J/ESC[H and ESC[K this unit's own ApplyAttr/
ClrScr/ClrEol (share/plang/units/Crt.pas) emit -- captured here over a pipe,
not a real terminal, which plang's own Write never gates on: piped stdout
is exactly what a lit RUN line captures, and this is a straight ANSI escape
check, not a raw-mode keyboard one (that half needs a real PTY -- see this
item's own crt-readkey-and-keypressed-drive-a-real-pty.pas sibling).  ESC is
translated to a plain 'E' via `tr` (not od -- this project's own
od --strict-whitespace lesson from Tier 3) so FileCheck can match it as
ordinary text.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | tr '\033' 'E' | FileCheck %s
*)
program ClrScrClrEol;
uses Crt;
begin
  ClrScr;
  Write('x');
  ClrEol;
  Writeln;
end.
(*
CHECK: E[0;37;40mE[2JE[HxE[0;37;40mE[K
*)
