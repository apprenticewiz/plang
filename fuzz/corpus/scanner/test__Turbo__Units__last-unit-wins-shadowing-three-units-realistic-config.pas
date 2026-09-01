(*
Turbo Tier 4 capstone (integration): last-used-unit-wins shadowing, proven
in a more realistic shape than
a-later-uses-clauses-unit-shadows-an-earlier-ones-same-named-export.pas's
own minimal two-unit, one-constant proof -- THREE units, each a plausible
stand-in for a real Turbo program's per-environment config module
(DevConfig/StagingConfig/ProdConfig), each exporting the SAME two names
(Environment, MaxRetries) with different values, `uses`d together by one
program in that order.  A bare read of either name has to resolve to
ProdConfig's own value (named last), matching real `fpc -Mtp` field
practice the same way the two-unit precedent test already confirmed for
one unit pair; explicit `UnitName.Value` qualification then reaches EACH
of the three individually, including the two that are shadowed at the bare
name -- the qualification half is what a program that actually needs a
NON-default environment's value (e.g. to log what dev's setting would have
been) depends on, not just an academic check.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bare Environment=prod
CHECK-NEXT:bare MaxRetries=5
CHECK-NEXT:DevConfig.Environment=dev
CHECK-NEXT:DevConfig.MaxRetries=1
CHECK-NEXT:StagingConfig.Environment=staging
CHECK-NEXT:StagingConfig.MaxRetries=3
CHECK-NEXT:ProdConfig.Environment=prod
CHECK-NEXT:ProdConfig.MaxRetries=5
*)

//--- devconfig.pas
unit DevConfig;
interface
const
  Environment = 'dev';
  MaxRetries = 1;
implementation
end.

//--- stagingconfig.pas
unit StagingConfig;
interface
const
  Environment = 'staging';
  MaxRetries = 3;
implementation
end.

//--- prodconfig.pas
unit ProdConfig;
interface
const
  Environment = 'prod';
  MaxRetries = 5;
implementation
end.

//--- main.pas
program ConfigShadow;
uses DevConfig, StagingConfig, ProdConfig;
begin
  Writeln('bare Environment=', Environment);
  Writeln('bare MaxRetries=', MaxRetries);
  Writeln('DevConfig.Environment=', DevConfig.Environment);
  Writeln('DevConfig.MaxRetries=', DevConfig.MaxRetries);
  Writeln('StagingConfig.Environment=', StagingConfig.Environment);
  Writeln('StagingConfig.MaxRetries=', StagingConfig.MaxRetries);
  Writeln('ProdConfig.Environment=', ProdConfig.Environment);
  Writeln('ProdConfig.MaxRetries=', ProdConfig.MaxRetries);
end.
