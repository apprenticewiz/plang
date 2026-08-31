(*
Turbo Tier 5, Cluster A item 7: pointer covariance is ONE-DIRECTIONAL --
confirmed against a local fpc -Mtp build (cov2.pas's own pointer analog)
that the reverse assignment (an ancestor-typed pointer's value into a
descendant-typed pointer variable) is still refused, matching the same
"Incompatible types" real Borland/FPC gives for the plain-VALUE case.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program PointerCovarianceIsOneDirectional;

type
  TAnimal = object
    Name: string;
  end;
  TDog = object(TAnimal)
    Breed: string;
  end;

var
  PA: ^TAnimal;
  PD: ^TDog;
begin
  PD := PA;
end.

(*
CHECK: error: cannot assign '^TAnimal' to variable of type '^TDog'
*)
