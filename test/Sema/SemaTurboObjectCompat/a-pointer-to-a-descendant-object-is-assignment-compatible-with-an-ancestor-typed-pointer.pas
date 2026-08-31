(*
Turbo Tier 5, Cluster A item 7: pointer covariance.  Confirmed against a
local fpc -Mtp build (cov1.pas) that a POINTER to a descendant object type
is assignment-compatible with an ancestor-typed pointer variable -- the
natural 'PA := @D;' idiom, which every earlier Tier 5 item had to work
around with a same-size variable typecast ('@TAnimal(D)') because this gap
was still open.  The reverse direction (an ancestor-typed pointer assigned
to a descendant-typed variable) is still refused: Sema::isAssignCompatible's
new Pointer-to-Object case only walks the descendant's OWN Parent chain
looking for the destination, never the other way.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s
*)

program PointerCovariance;

type
  TAnimal = object
    Name: string;
  end;
  TDog = object(TAnimal)
    Breed: string;
  end;

var
  D: TDog;
  PA: ^TAnimal;
begin
  PA := @D;
end.
