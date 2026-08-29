(*
Regression gate, the same shape as
exitcode-is-turbo-only-not-available-under-iso7185-or-extended-pascal.pas:
SizeOf/High/Low/Hi/Lo/Swap/Include/Exclude/Inc/Dec/FillChar/Move are all
declared TP-only in Builtins.def (Dialects = TP), so each is a required
identifier under every dialect (Sema::registerBuiltins declares every
Builtins.def name everywhere) but refused BY NAME under -std=iso7185 and
-std=iso10206 via checkEPOnly's err_turbo_required_name, rather than
falling back to "undefined function/procedure" the way a name Builtins.def
never declared at all would.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'sizeof' is a Turbo Pascal extension
CHECK: 'high' is a Turbo Pascal extension
CHECK: 'low' is a Turbo Pascal extension
CHECK: 'hi' is a Turbo Pascal extension
CHECK: 'lo' is a Turbo Pascal extension
CHECK: 'swap' is a Turbo Pascal extension
CHECK: 'include' is a Turbo Pascal extension
CHECK: 'exclude' is a Turbo Pascal extension
CHECK: 'inc' is a Turbo Pascal extension
CHECK: 'dec' is a Turbo Pascal extension
CHECK: 'fillchar' is a Turbo Pascal extension
CHECK: 'move' is a Turbo Pascal extension
*)

program p;
type
  tcolor = (red, green, blue);
  tcolorset = set of tcolor;
var
  i, n, cnt: integer;
  s: tcolorset;
  buf: array [1 .. 4] of char;
begin
  n := sizeof(integer);
  n := high(integer);
  n := low(integer);
  n := hi(i);
  n := lo(i);
  n := swap(i);
  include(s, red);
  exclude(s, red);
  inc(i);
  dec(i);
  fillchar(buf, 1, ' ');
  move(buf, buf, 1);
  cnt := n;
end.
