(*
Turbo Tier 5, Cluster A item 0: parsing only.  Confirmed against a local
fpc -Mtp build that a Turbo object method's real BODY is written
out-of-line, elsewhere in the same declaration section, qualified by its
owning type -- 'procedure TAnimal.Speak; begin ... end;', the same
'TypeName.MethodName' shape as this codebase's own dotted heading now
parses -- repeating the full heading (parameters, result type) exactly like
an ordinary 'forward' declaration's real body would.  Parser::parseProcDecl
reads the qualifier into ProcDecl::OwnerType and the method name into
ProcDecl::Name; this checks both a constructor's, an ordinary method's, and
a destructor's out-of-line body, across two different object types in the
same program, each producing its own AST-level top-level ProcDecl (not
nested inside the type declaration at all).
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program OutOfLineBodies;

type
  TAnimal = object
    Name: string;
    constructor Init(N: string);
    procedure Speak; virtual;
    destructor Done; virtual;
  end;

  TDog = object(TAnimal)
  private
    Breed: string;
  public
    procedure Speak; virtual;
    procedure Bark;
  end;

constructor TAnimal.Init(N: string);
begin
  Name := N;
end;

procedure TAnimal.Speak;
begin
end;

destructor TAnimal.Done;
begin
end;

procedure TDog.Speak;
begin
end;

procedure TDog.Bark;
begin
end;

begin
end.

(*
CHECK:(program OutOfLineBodies
CHECK-NEXT:  (typedef TAnimal (object (public (Name string)) (public constructor Init ((N string))) (public procedure Speak () virtual) (public destructor Done () virtual)))
CHECK-NEXT:  (typedef TDog (object (ancestor TAnimal) (private (Breed string)) (public procedure Speak () virtual) (public procedure Bark ())))
CHECK-NEXT:  (constructor TAnimal.Init ((N string))
CHECK-NEXT:    (compound
CHECK-NEXT:      (assign Name N)))
CHECK-NEXT:  (procedure TAnimal.Speak ()
CHECK-NEXT:    (compound))
CHECK-NEXT:  (destructor TAnimal.Done ()
CHECK-NEXT:    (compound))
CHECK-NEXT:  (procedure TDog.Speak ()
CHECK-NEXT:    (compound))
CHECK-NEXT:  (procedure TDog.Bark ()
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
