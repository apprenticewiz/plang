(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("Virtual constructors are only supported in class object model") that a
TP7 object-model constructor can never be declared 'virtual' -- only a
destructor (or an ordinary method) may be; a constructor is always static.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program VirtualConstructor;

type
  TAnimal = object
    constructor Init; virtual;
  end;

constructor TAnimal.Init;
begin
end;

begin
end.

(*
CHECK: error: constructor 'Init' may not be declared 'virtual'
*)
