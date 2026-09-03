(*
Issue #790: mutual `uses` THROUGH IMPLEMENTATION (MCycA's implementation
'uses' MCycB, and MCycB's implementation 'uses' MCycA right back) must not
infinite-loop, must not run either unit's init section twice, and must
settle on SOME defensible order.

The order this repo settled on: it falls straight out of compile order,
mirroring EP's own already-shipped module-initialiser design (each unit's
`__plang_init_<name>` guards itself and calls its own direct dependencies'
init functions first; the guard makes calling one twice, from anywhere,
harmless -- see emitUnitInitFn's own header comment, CodeGenProcs.cpp).
MCycB is compiled FIRST here, before MCycA has ever been compiled -- so at
that point Sema can only resolve MCycB's own 'uses MCycA' through its .pas
source-reparse fallback (no mcyca.tui exists yet), and CodeGen deliberately
never calls another unit's init function through that fallback path (no
guaranteed matching .o -- see Sema::unitInitCallNames' own comment). So
MCycB's own init does NOT call MCycA's. MCycA is compiled SECOND, once
mcycb.tui already exists as a real published interface -- so MCycA's own
'uses MCycB' resolves for real, and MCycA's own init DOES call MCycB's
first. Net effect, confirmed below: B's init prints before A's, each
exactly once, and the program (which itself 'uses MCycA, MCycB' in THAT
order) sees both already initialized by the time its own `begin` runs.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/mcycb.pas -o %t.dir/mcycb.o
RUN: %plang -std=turbo -I%t.dir -c %t.dir/mcyca.pas -o %t.dir/mcyca.o
RUN: rm %t.dir/mcyca.pas %t.dir/mcycb.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/mcyca.o %t.dir/mcycb.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:B init
CHECK-NEXT:A init
CHECK-NEXT:main, ARan=1 BRan=1
*)

//--- mcycb.pas
unit MCycB;

interface

var BRan: Integer;

implementation

uses MCycA;

begin
  BRan := 1;
  Writeln('B init');
end.

//--- mcyca.pas
unit MCycA;

interface

var ARan: Integer;

implementation

uses MCycB;

begin
  ARan := 1;
  Writeln('A init');
end.

//--- main.pas
program MutualProg;
uses MCycA, MCycB;
begin
  Writeln('main, ARan=', ARan, ' BRan=', BRan);
end.
