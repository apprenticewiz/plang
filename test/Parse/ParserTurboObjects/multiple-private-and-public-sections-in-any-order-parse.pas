(*
Turbo Tier 5, Cluster A item 0: parsing only.  Confirmed against a local
fpc -Mtp build that real Turbo Pascal (Borland TP 7.0+) objects use
SECTION-based visibility with no restriction on how many 'private'/'public'
sections appear or in what order -- private-then-public-then-private-again
compiled cleanly there.  So Parser::parseObjectType tracks a running
"current visibility" rather than a fixed "one public part, one optional
trailing private part" shape, and each ObjectMember is stamped with the
visibility in force when it was parsed.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program MultipleSections;

type
  TWidget = object
  private
    A: integer;
  public
    B: integer;
  private
    C: integer;
  end;

begin
end.

(*
CHECK:(program MultipleSections
CHECK-NEXT:  (typedef TWidget (object (private (A integer)) (public (B integer)) (private (C integer))))
CHECK-NEXT:  (compound))
*)
