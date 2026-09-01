(*
Issue #682: an explicit 'inherited Method(args)' call whose ancestor
implementation lives in a DIFFERENT translation unit used to build its own
"not yet declared here" fallback declaration from the CALL SITE's own
written argument EXPRESSION types (Arg->ResolvedType) rather than the
ancestor method's own RESOLVED formal parameter list -- indistinguishable
from correct for a plain value parameter (an Integer actual looks the same
either way), but wrong for a 'var' parameter: the fallback declared it as
an ordinary by-value Integer instead of a pointer, so the caller passed the
actual's VALUE while the real (separately-compiled) callee -- compiled
with the correct 'var' shape -- read it as an address and wrote through it,
corrupting memory (confirmed via disassembly on the original repro: the
caller's `movzwl` of a 16-bit value where the real callee's own `movw
(%rsi),%ax` expected a pointer).

Unit UBase declares TBase.Store(var x: Integer), a VIRTUAL method with a
real out-of-line body compiled entirely inside UBase's own translation
unit.  The program declares TDer = object(TBase), OVERRIDES Store, and
calls 'inherited Store(x)' from within that override -- reaching TBase's
own implementation across the unit boundary.  x is incremented by both
levels; if the 'var' parameter crossed the boundary correctly, the caller's
own variable v is incremented twice (once by each level) and the resulting
value is observable, proving the ancestor call really did write through
the caller's own storage rather than a throwaway copy (or crash outright,
the original repro's own actual failure mode).

Both compiled entirely separately (the unit's source is deleted before the
program is compiled), matching this project's own established "strongest
possible proof" separate-compilation pattern -- and specifically covering
the #182/#570 cross-unit call-marshalling surface this issue's own filing
named as the likely fault line.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/ubase.pas -o %t.dir/ubase.o
RUN: rm %t.dir/ubase.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/ubase.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:16
*)

//--- ubase.pas
unit UBase;

interface

type
  TBase = object
    procedure Store(var x: Integer); virtual;
  end;

implementation

procedure TBase.Store(var x: Integer);
begin
  x := x + 1;
end;

end.

//--- main.pas
program CrossUnitInheritedVarParam;

uses UBase;

type
  TDer = object(TBase)
    procedure Store(var x: Integer); virtual;
  end;

procedure TDer.Store(var x: Integer);
begin
  inherited Store(x);
  x := x + 10;
end;

var
  D: TDer;
  v: Integer;
begin
  v := 5;
  D.Store(v);
  writeln(v);
end.
