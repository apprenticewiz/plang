(*
Issue #514's own fix extended to its natural third call site.  Not one of
the issue's own two repros, but the exact same underlying bug: emitBound-
MethodCall (CGProcCall.cpp) reads a receiver's `_vptr` through the
identical GEP-load-GEP-load sequence emitMethodCallExpr/emitMethodCallStmt
do, reached here by Dispose(p, Done) on a VIRTUAL destructor -- and a plain
New(p) never having been New(p, Init(...))'d leaves that same slot NULL
(plang_new's own calloc), not a real VMT address, for exactly the same
reason the sibling tests in this directory cover for a virtual METHOD
call.  Confirmed segfaulting on unmodified main before this fix (exit 139,
core dumped) and confirmed against a local `fpc -Mtp` build: "Runtime
error 216", exit 216, no core dump -- byte-for-byte the same numbered trap
as a virtual method call gets, not a different one.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %checkexit 216 %run %t.O0 > %t.O0.out 2> %t.O0.err
RUN: FileCheck %s < %t.O0.out
RUN: FileCheck --check-prefix=ERR %s < %t.O0.err
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %checkexit 216 %run %t.O2 > %t.O2.out 2> %t.O2.err
RUN: FileCheck %s < %t.O2.out
RUN: FileCheck --check-prefix=ERR %s < %t.O2.err
*)

program plainnew3;
type
  TAnimal = object
    procedure Speak; virtual;
    destructor Done; virtual;
  end;
  TDog = object(TAnimal)
    destructor Done; virtual;
  end;
  PDog = ^TDog;
var
  P: PDog;

procedure TAnimal.Speak;
begin
  writeln('Animal speaks');
end;

destructor TAnimal.Done;
begin
  writeln('Animal Done');
end;

destructor TDog.Done;
begin
  writeln('Dog Done');
end;

begin
  New(P);
  writeln('about to dispose through an unstamped vptr');
  Dispose(P, Done);
  writeln('unreachable');
end.

(*
CHECK:about to dispose through an unstamped vptr
CHECK-NOT:unreachable
ERR:Runtime error 216
*)
