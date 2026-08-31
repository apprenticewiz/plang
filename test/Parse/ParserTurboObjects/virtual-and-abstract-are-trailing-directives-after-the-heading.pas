(*
Turbo Tier 5, Cluster A item 0: parsing only.  Confirmed against a local
fpc -Mtp build that 'virtual' and 'abstract' are TRAILING directives
written after the method heading's own ';', exactly like this codebase's
existing 'forward' directive (Parser::parseProcDecl) -- never written
before 'procedure'/'function'/'constructor'/'destructor'.  Both may appear
together ('procedure Draw; virtual; abstract;'), each terminated by its own
';' in turn.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program AbstractOnly;

type
  TShape = object
    procedure Draw; virtual; abstract;
  end;

begin
end.

(*
CHECK:(program AbstractOnly
CHECK-NEXT:  (typedef TShape (object (public procedure Draw () virtual abstract)))
CHECK-NEXT:  (compound))
*)
