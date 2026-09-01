(*
Turbo Tier 5 capstone (Cluster C item 9): a single, cohesive integration
scenario exercising the tier's own object model at realistic scale, in one
program rather than N disconnected micro-tests -- mirroring how the Tier 3
(test/Turbo/*.pas) and Tier 4 (test/Turbo/Units/*.pas) capstones themselves
combined already-shipped, individually-tested behaviors into one larger
proof rather than re-deriving each in isolation.

A small "employee roster" models a genuine 3-level hierarchy: TEmployee (a
Name field, a PRIVATE Salary), TManager = object(TEmployee) (adds a PRIVATE
DirectReports), TExecutive = object(TManager) (adds a PRIVATE StockOptions).
All three combine, in one program, everything Cluster A/B (items 0-8)
proved individually:

  - Virtual dispatch through an ANCESTOR-typed pointer (PEmployee) actually
    reaching each concrete type's own override, all 3 levels deep at once:
    a 'Roster' array of PEmployee holds a plain TEmployee, a TManager, and
    an TExecutive side by side, and one dispatch call
    (Roster[I]^.Describe) reaches the correct one of three different
    override bodies purely from the object's own runtime type -- the
    single most important thing this capstone proves, confirmed by real
    WriteLn'd output below, not just successful compilation.
  - A real 2-hop 'inherited' STATEMENT chain: TExecutive.Describe calls
    'inherited Describe' (reaching TManager.Describe), which itself calls
    'inherited Describe' (reaching TEmployee.Describe) -- each level's own
    contribution actually prints, so the CHECK block below can see all
    three ran, in the right order, for the same one call. (Cluster A's own
    item 5 tests chain 'inherited' at most 2 levels total, e.g. Dog calling
    Animal; nothing in the existing suite chains it 3 deep in one call.)
    TManager.Describe also reads 'Salary' completely unqualified, even
    though Salary is PRIVATE and declared two levels up in TEmployee --
    ordinary same-module private access (item 7), reused here rather than
    re-proven in isolation.
  - New(P, Init(...))'s full lifecycle: three successful constructions
    (Bob/Maria/Alice, one per level) feeding the roster, AND a failing one
    (Bogus, an TExecutive whose negative AStockOptions makes TExecutive.Init
    call Fail -- but only *after* its own 'inherited Init' already
    succeeded and set the inherited portion of the object, the same
    already-proven "ancestor's own share completes, descendant's own check
    runs after, Fail is reached last" shape item 6's own precedent test
    uses) -- confirmed by Bogus's own pointer coming back nil with NO
    "TEmployee.Done"/"TManager.Done"/"TExecutive.Done: releasing Bogus"
    ever printed (no destructor runs on a failed construction, matching
    item 6's own contract).
  - Dispose(P, Done)'s own VMT dispatch: 'Done' is declared 'virtual' at
    all three levels (unlike item 6's own precedent test, which never
    disposes through an ancestor-typed pointer and so never needed a
    virtual destructor at all) specifically so the closing disposal loop,
    which walks the SAME ancestor-typed Roster array the dispatch section
    above already used, reaches each instance's own real destructor chain
    -- Alice tears down Executive->Manager->Employee, Maria tears down
    Manager->Employee, Bob tears down only Employee -- purely from runtime
    type, exactly mirroring the dispatch section's own proof but for
    cleanup instead of behavior.

The two 'method ... hides the inherited method' warnings this file
compiles with (TManager.Init and TExecutive.Init, at their own
declarations -- Done is declared 'virtual' at all three levels, so it
correctly overrides rather than hiding, and warns about nothing) are the
already-documented, already-accepted cosmetic false positive for
same-named, differently-signed, non-virtual constructors across levels
(docs/turbo.md's own "Object types" section; confirmed a local `fpc -Mtp`
build stays silent on the identical construct) -- not a new finding, not
suppressed here for the same reason
fail-inside-a-constructor-leaves-the-callers-pointer-nil.pas
(test/CodeGen/CodeGenTurboConstructors/) already carries the identical
warning uncommented-on.

Salary/DirectReports/StockOptions are kept well inside Turbo's 16-bit
Integer range (TP7's own 'Integer', -32768..32767, is still the plain
Integer this project's Tier 1 milestone chose) -- deliberately small
"thousands of dollars" figures, not literal salaries, so no value here
ever risks wraparound.

Confirmed byte-for-byte against a local `fpc -Mtp` 3.2.2 build (this file
plus its own leading mode-tp compatibility pragma, otherwise identical)
before being written down here, the same discipline every other empirical
claim in this tier's own tests already follows.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program EmployeeRosterIntegration;

type
  PEmployee = ^TEmployee;
  TEmployee = object
    Name: string[30];
  private
    Salary: Integer;
  public
    constructor Init(AName: string; ASalary: Integer);
    destructor Done; virtual;
    procedure Describe; virtual;
  end;

  PManager = ^TManager;
  TManager = object(TEmployee)
  private
    DirectReports: Integer;
  public
    constructor Init(AName: string; ASalary, ADirectReports: Integer);
    destructor Done; virtual;
    procedure Describe; virtual;
  end;

  PExecutive = ^TExecutive;
  TExecutive = object(TManager)
  private
    StockOptions: Integer;
  public
    constructor Init(AName: string; ASalary, ADirectReports, AStockOptions: Integer);
    destructor Done; virtual;
    procedure Describe; virtual;
  end;

constructor TEmployee.Init(AName: string; ASalary: Integer);
begin
  Name := AName;
  if ASalary <= 0 then
  begin
    writeln('TEmployee.Init: rejecting non-positive salary for ', AName);
    Fail;
  end;
  Salary := ASalary;
end;

destructor TEmployee.Done;
begin
  writeln('TEmployee.Done: releasing ', Name);
end;

procedure TEmployee.Describe;
begin
  writeln('Employee ', Name, ', salary ', Salary);
end;

constructor TManager.Init(AName: string; ASalary, ADirectReports: Integer);
begin
  inherited Init(AName, ASalary);
  DirectReports := ADirectReports;
end;

destructor TManager.Done;
begin
  writeln('TManager.Done: releasing ', Name);
  inherited Done;
end;

procedure TManager.Describe;
begin
  inherited Describe;
  writeln('  manages ', DirectReports, ' direct report(s); base salary on file: ', Salary);
end;

constructor TExecutive.Init(AName: string; ASalary, ADirectReports, AStockOptions: Integer);
begin
  inherited Init(AName, ASalary, ADirectReports);
  if AStockOptions < 0 then
  begin
    writeln('TExecutive.Init: rejecting negative stock options for ', AName);
    Fail;
  end;
  StockOptions := AStockOptions;
end;

destructor TExecutive.Done;
begin
  writeln('TExecutive.Done: releasing ', Name);
  inherited Done;
end;

procedure TExecutive.Describe;
begin
  inherited Describe;
  writeln('  holds ', StockOptions, ' stock option(s)');
end;

var
  Roster: array[1..3] of PEmployee;
  I: Integer;
  PEmp: PEmployee;
  PMgr: PManager;
  PExec: PExecutive;
  PBad: PExecutive;
begin
  New(PEmp, Init('Bob', 450));
  New(PMgr, Init('Maria', 850, 4));
  New(PExec, Init('Alice', 1200, 6, 500));

  Roster[1] := PEmp;
  Roster[2] := PMgr;   { pointer covariance: ^TManager assigned to a ^TEmployee-typed slot }
  Roster[3] := PExec;  { pointer covariance: ^TExecutive assigned to a ^TEmployee-typed slot }

  writeln('--- roster (virtual dispatch through TEmployee-typed pointers) ---');
  for I := 1 to 3 do
    Roster[I]^.Describe;

  writeln('--- Fail path: constructing an executive with negative stock options ---');
  New(PBad, Init('Bogus', 500, 1, -1));
  if PBad = nil then
    writeln('construction correctly failed, PBad is nil')
  else
  begin
    writeln('WRONG: PBad is not nil');
    Dispose(PBad, Done);
  end;

  writeln('--- disposing the roster through ancestor-typed pointers (virtual destructor dispatch) ---');
  for I := 3 downto 1 do
    Dispose(Roster[I], Done);

  writeln('done');
end.

(*
CHECK:--- roster (virtual dispatch through TEmployee-typed pointers) ---
CHECK-NEXT:Employee Bob, salary 450
CHECK-NEXT:Employee Maria, salary 850
CHECK-NEXT:  manages 4 direct report(s); base salary on file: 850
CHECK-NEXT:Employee Alice, salary 1200
CHECK-NEXT:  manages 6 direct report(s); base salary on file: 1200
CHECK-NEXT:  holds 500 stock option(s)
CHECK-NEXT:--- Fail path: constructing an executive with negative stock options ---
CHECK-NEXT:TExecutive.Init: rejecting negative stock options for Bogus
CHECK-NEXT:construction correctly failed, PBad is nil
CHECK-NEXT:--- disposing the roster through ancestor-typed pointers (virtual destructor dispatch) ---
CHECK-NEXT:TExecutive.Done: releasing Alice
CHECK-NEXT:TManager.Done: releasing Alice
CHECK-NEXT:TEmployee.Done: releasing Alice
CHECK-NEXT:TManager.Done: releasing Maria
CHECK-NEXT:TEmployee.Done: releasing Maria
CHECK-NEXT:TEmployee.Done: releasing Bob
CHECK-NEXT:done
*)
