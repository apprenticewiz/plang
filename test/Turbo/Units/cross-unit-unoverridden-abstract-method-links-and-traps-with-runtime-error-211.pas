(*
Issue #618: a unit's own INTERFACE section declares TA with an ABSTRACT
virtual method (Speak) and a real constructor (Init).  A program that
`uses` the unit declares TD = object(TA), overriding NOTHING, then
allocates one with the extended-syntax 'New(p, Init)' (running TA's own
constructor) and calls p^.Speak through it.

Before this fix, the unit's own compile never even EMITTED a defined
function for TA.Speak's abstract stub at all: emitAllProcedures's
abstract-stub pre-pass only ever walked a block it was called on
(ImplementationBlock, for a unit), and TA is declared in the INTERFACE
section, which that pre-pass never sees.  The consuming program's own VMT
for TD referenced the stub's mangled name (pas_uabs$ta$speak) as an
external symbol, so linking failed outright with an undefined symbol,
rather than either compiling clean (matching a local fpc -Mtp build, which
links and TRAPS Runtime error 211 the moment Speak is actually called
through the VMT) or being rejected at compile time.

Both compiled entirely separately (the unit's source is deleted before the
program is compiled), the same "strongest possible proof" separate-
compilation pattern this directory's other cross-unit tests already use.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/uabs.pas -o %t.dir/uabs.o
RUN: rm %t.dir/uabs.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/uabs.o -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
CHECK:calling abstract method now
CHECK-NOT:unreachable
ERR:Runtime error 211
*)

//--- uabs.pas
unit UAbs;

interface

type
  TA = object
    constructor Init;
    procedure Speak; virtual; abstract;
  end;

implementation

constructor TA.Init;
begin
end;

end.

//--- main.pas
program CrossUnitAbstractMethod;

uses UAbs;

type
  TD = object(TA)
  end;
  PD = ^TD;

var
  p: PD;
begin
  New(p, Init);
  writeln('calling abstract method now');
  p^.Speak;
  writeln('unreachable');
end.
