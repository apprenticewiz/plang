(*
Issue #621: method-call resolution used to be keyed by a bare-name
composite string ("<lowercase type>.<lowercase method>", Sema::
objectMethodKey) looked up through the ORDINARY SCOPE CHAIN at the call
site (Symtab.lookup), rather than by the actual receiver TYPE'S OWN
identity.  Two unrelated object types sharing a spelling -- a program
re-declaring its own 'TA' after 'uses'-ing a unit that already declares a
'TA' -- register under the IDENTICAL key in different scopes, so a call
through a variable of the UNIT's TA got checked against the PROGRAM's own
(nearer-in-scope) TA instead: a spurious 'type mismatch for parameter'
diagnostic here (the two Speak overloads take incompatible parameter
types, Integer vs. string), and -- had the two signatures merely been
COMPATIBLE rather than mismatched outright -- silently wrong-typed
marshalling, since CodeGen already re-derives the real callee correctly
from the type graph and would have disagreed with what Sema checked.
Fixed by resolving a method call by walking the receiver's own RESOLVED
Type's ancestor chain directly (Sema::findObjectMethod), exactly the way
CodeGen's own methodOwnerType/methodEntryOf helpers already do -- never
confused by a same-spelled but unrelated type declared anywhere else in
the program.  Confirmed against a local `fpc -Mtp` build: this compiles
and runs cleanly there too.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/shadowedunit.pas -o %t.dir/shadowedunit.o
RUN: rm %t.dir/shadowedunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/prog.pas %t.dir/shadowedunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:U.TA.Speak(Integer): 5
*)

//--- shadowedunit.pas
unit ShadowedUnit;

interface

type
  TA = object
    n: Integer;
    procedure Speak(n: Integer);
  end;

implementation

procedure TA.Speak(n: Integer);
begin
  writeln('U.TA.Speak(Integer): ', n);
end;

end.

//--- prog.pas
program Prog;

uses ShadowedUnit;

type
  { Shadows ShadowedUnit's own TA -- a DIFFERENT type that merely happens
    to share the spelling and also declares a method named 'Speak', with
    an incompatible parameter type. }
  TA = object
    s: string;
    procedure Speak(s: string);
  end;

procedure TA.Speak(s: string);
begin
  writeln('Prog.TA.Speak(string): ', s);
end;

var
  { Explicitly the UNIT's own TA, not the program's re-declared one. }
  UA: ShadowedUnit.TA;
begin
  UA.Speak(5);
end.
