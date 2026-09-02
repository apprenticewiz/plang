(*
Issue #579: this directory's sibling test (dispose-with-a-virtual-
destructor-on-a-plain-new-object-...) covers Dispose(P, Done) on a plain
New(p) that was never New(p, Init(...))'d -- P itself is non-nil, but its
`_vptr` slot is unstamped/NULL, and emitBoundMethodCall's own vptr-nil
check (RangeGuards.emitNilCheck(vmt), added for issue #514) catches that.

This is the OTHER, still-open way to reach the same call with a bad
receiver: P itself is nil.  Dispose(P, Done)'s own codegen
(CGProcCall.cpp) used to read P's raw pointer value with no nil check at
all and hand it straight to emitBoundMethodCall, which -- for a VIRTUAL
destructor -- computes `self.vptr.addr` via a GEP on selfPtr and LOADS
through it before ever reaching the existing vptr-nil check, so a nil
selfPtr dereferenced near address zero and segfaulted (exit 139, core
dumped) instead of trapping. Ordinary `P^.M;` never hit this because
EmitLValue's own pointer-dereference handling nil-checks P itself before
any field/vptr GEP is built; Dispose(P, Done) never goes through that
path, since it takes P's own value directly rather than P^.

Confirmed segfaulting on unmodified main before this fix and confirmed
against a local `fpc -Mtp` build: "Runtime error 216", exit 216, no core
dump.
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

program disposenil;
type
  TA = object
    constructor Init;
    destructor Done; virtual;
  end;
var
  P: ^TA;

constructor TA.Init;
begin
end;

destructor TA.Done;
begin
  writeln('TA.Done');
end;

begin
  P := nil;
  writeln('about to Dispose(nil, Done)');
  Dispose(P, Done);
  writeln('unreachable');
end.

(*
CHECK:about to Dispose(nil, Done)
CHECK-NOT:unreachable
ERR:Runtime error 216
*)
