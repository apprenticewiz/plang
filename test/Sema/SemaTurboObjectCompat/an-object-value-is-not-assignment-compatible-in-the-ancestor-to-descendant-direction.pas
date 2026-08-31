(*
Turbo Tier 5, Cluster A item 7: object VALUE covariance is one-directional,
just like the pointer case.  Confirmed against a local fpc -Mtp build
(cov2.pas): 'D := A;' for 'A: TAnimal; D: TDog;' is refused ("Incompatible
types: got TAnimal expected TDog"), even though the reverse 'A := D;' is
ordinary value slicing.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program ObjectValueCovarianceIsOneDirectional;

type
  TAnimal = object
    Name: string;
  end;
  TDog = object(TAnimal)
    Breed: string;
  end;

var
  A: TAnimal;
  D: TDog;
begin
  D := A;
end.

(*
CHECK: error: cannot assign 'TAnimal' to variable of type 'TDog'
*)
