(*
Turbo Tier 5 capstone (Cluster C item 9): the cross-unit half of this
tier's own capstone, complementing three-units-cross-unit-object-
inheritance-and-virtual-dispatch.pas (Cluster B item 8's own capstone-scale
proof, right next to this file) rather than repeating it -- that test
already proves cross-unit inheritance, virtual dispatch through an
ancestor-typed pointer, 'inherited' reaching back across the unit
boundary, and a cross-unit method call with a string argument, all with
plain stack-declared objects. It never once calls New/Dispose or exercises
Fail across a unit boundary, and its own destructor coverage is zero (its
TAnimal/TDog have no destructor at all) -- exactly the gap this file
fills, reusing that same test's own "two real units, each compiled
entirely on its own via a fresh `plang -std=turbo -c`, sources DELETED
before the program that links them is ever compiled" shape (Tier 4's own
established strongest-possible-proof pattern for separate compilation).

A different domain (vehicles, not animals) so this reads as its own
scenario rather than a find-and-replace of the sibling test:

  - Unit UnitVehicle declares the ROOT object type TVehicle: a PUBLIC Make
    field, a PRIVATE Wheels field (counted, at its real offset, by a
    descendant declared in a DIFFERENT unit below -- the same cross-unit
    private-field-layout proof item 8's own capstone already established,
    reused rather than re-derived here), a validating constructor (Fails
    on a non-positive wheel count), and a 'virtual' destructor and
    'Describe'.
  - Unit UnitElectricCar 'uses' UnitVehicle and declares TElectricCar =
    object(TVehicle), adding a PRIVATE BatteryKwh field, its
    own validating constructor (calls 'inherited Init' across the unit
    boundary FIRST, then Fails on a non-positive battery size -- the same
    "ancestor's own share completes, descendant's own check runs after"
    shape this tier's other Fail tests already use, now crossing a unit
    boundary), and its own 'virtual' 'Done'/'Describe' overrides, each
    calling 'inherited' back into UnitVehicle.
  - Program ProgVehicles 'uses' both units directly (confirmed elsewhere in
    this tier, against a real `fpc -Mtp` build, that 'uses' is not
    transitive for a type name -- unchanged here, just relied upon), heap-
    allocates one plain TVehicle and one TElectricCar via New(P,
    Init(...)), stores both in one 'Fleet' array of the ANCESTOR pointer
    type PVehicle (cross-unit pointer covariance), dispatches Describe
    through that ancestor-typed array (reaching each concrete type's own
    override, across the unit boundary), drives one failing New(...,
    Init(...)) across the unit boundary (confirming the caller's own
    pointer ends up nil, no destructor ever runs for it), and finally
    Disposes the whole Fleet back through the SAME ancestor-typed array --
    proving Dispose's own VMT dispatch (not just New's construction side)
    also survives a unit boundary: TElectricCar's instance tears down
    ElectricCar->Vehicle, the plain TVehicle instance tears down only
    Vehicle, purely from each pointer's own runtime type.

Confirmed byte-for-byte against a local `fpc -Mtp` 3.2.2 build (the three
chunks below, concatenated into their own files, plus a leading
'{$mode tp}' on each) before being written down here.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/unitvehicle.pas -o %t.dir/unitvehicle.o
RUN: rm %t.dir/unitvehicle.pas
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unitelectriccar.pas -o %t.dir/unitelectriccar.o
RUN: rm %t.dir/unitelectriccar.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/progvehicles.pas %t.dir/unitvehicle.o %t.dir/unitelectriccar.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:--- fleet (virtual dispatch across the unit boundary) ---
CHECK-NEXT:Wagon has 4 wheel(s)
CHECK-NEXT:Volt has 4 wheel(s)
CHECK-NEXT:  electric, 60 kWh battery
CHECK-NEXT:--- Fail path across the unit boundary ---
CHECK-NEXT:TElectricCar.Init: rejecting non-positive battery size for Bogus
CHECK-NEXT:construction correctly failed, PBadCar is nil
CHECK-NEXT:--- disposing the fleet through ancestor-typed pointers (virtual destructor dispatch across the unit boundary) ---
CHECK-NEXT:TElectricCar.Done: releasing Volt
CHECK-NEXT:TVehicle.Done: releasing Volt
CHECK-NEXT:TVehicle.Done: releasing Wagon
CHECK-NEXT:done
*)

//--- unitvehicle.pas
unit UnitVehicle;

interface

type
  PVehicle = ^TVehicle;
  TVehicle = object
    Make: string;
  private
    Wheels: Integer;
  public
    constructor Init(AMake: string; AWheels: Integer);
    destructor Done; virtual;
    procedure Describe; virtual;
  end;

implementation

constructor TVehicle.Init(AMake: string; AWheels: Integer);
begin
  Make := AMake;
  if AWheels <= 0 then
  begin
    writeln('TVehicle.Init: rejecting non-positive wheel count for ', AMake);
    Fail;
  end;
  Wheels := AWheels;
end;

destructor TVehicle.Done;
begin
  writeln('TVehicle.Done: releasing ', Make);
end;

procedure TVehicle.Describe;
begin
  writeln(Make, ' has ', Wheels, ' wheel(s)');
end;

end.

//--- unitelectriccar.pas
unit UnitElectricCar;

interface

uses UnitVehicle;

type
  PElectricCar = ^TElectricCar;
  TElectricCar = object(TVehicle)
  private
    BatteryKwh: Integer;
  public
    constructor Init(AMake: string; AWheels, ABatteryKwh: Integer);
    destructor Done; virtual;
    procedure Describe; virtual;
  end;

implementation

constructor TElectricCar.Init(AMake: string; AWheels, ABatteryKwh: Integer);
begin
  inherited Init(AMake, AWheels);
  if ABatteryKwh <= 0 then
  begin
    writeln('TElectricCar.Init: rejecting non-positive battery size for ', AMake);
    Fail;
  end;
  BatteryKwh := ABatteryKwh;
end;

destructor TElectricCar.Done;
begin
  writeln('TElectricCar.Done: releasing ', Make);
  inherited Done;
end;

procedure TElectricCar.Describe;
begin
  inherited Describe;
  writeln('  electric, ', BatteryKwh, ' kWh battery');
end;

end.

//--- progvehicles.pas
program ProgVehicles;

uses UnitVehicle, UnitElectricCar;

var
  Fleet: array[1..2] of PVehicle;
  I: Integer;
  PCar: PElectricCar;
  PPlain: PVehicle;
  PBadCar: PElectricCar;
begin
  New(PPlain, Init('Wagon', 4));
  New(PCar, Init('Volt', 4, 60));

  Fleet[1] := PPlain;
  Fleet[2] := PCar;   { cross-unit pointer covariance: ^TElectricCar -> ^TVehicle }

  writeln('--- fleet (virtual dispatch across the unit boundary) ---');
  for I := 1 to 2 do
    Fleet[I]^.Describe;

  writeln('--- Fail path across the unit boundary ---');
  New(PBadCar, Init('Bogus', 4, -1));
  if PBadCar = nil then
    writeln('construction correctly failed, PBadCar is nil')
  else
    writeln('WRONG: PBadCar is not nil');

  writeln('--- disposing the fleet through ancestor-typed pointers (virtual destructor dispatch across the unit boundary) ---');
  for I := 2 downto 1 do
    Dispose(Fleet[I], Done);

  writeln('done');
end.
