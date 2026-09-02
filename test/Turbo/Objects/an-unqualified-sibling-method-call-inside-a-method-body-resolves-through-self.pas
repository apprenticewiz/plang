(*
Issue #571: a completely UNQUALIFIED method call statement inside another
method's own body -- 'Who;' / 'Who();', no 'Self.' prefix at all -- used to
fail to resolve ('undefined procedure'), because pushMethodSelfScope
(Sema.cpp) exposed a method body's implicit Self context for FIELDS
(defining each one as a bare Var symbol in the pushed scope) but never for
the owning type's own METHODS, so checkCallStmt's plain Symtab.lookup found
nothing.  'Self.Who;'/'Self.Who();' (explicit qualifier) already worked
correctly, including virtual dispatch, which is what makes this a
resolution gap rather than a dispatch one: both bare forms below must
dispatch to TC.Who (the actual runtime type), exactly like the already-
working explicit-Self forms do -- confirmed against a local `fpc -Mtp`
build of this identical program.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program BareSelfCallStmt;

type
  TA = object
    constructor Init;
    procedure Who; virtual;
    procedure Test1;
    procedure Test2;
    procedure Test3;
  end;
  TC = object(TA)
    procedure Who; virtual;
  end;

constructor TA.Init; begin end;
procedure TA.Who; begin writeln('  -> TA.Who'); end;
procedure TC.Who; begin writeln('  -> TC.Who'); end;

{ bare unqualified call, no parens }
procedure TA.Test1;
begin
  writeln('Test1 (bare, no parens):');
  Who;
end;

{ bare unqualified call, WITH parens }
procedure TA.Test2;
begin
  writeln('Test2 (bare, with parens):');
  Who();
end;

{ a method calling itself recursively, unqualified, must resolve too --
  Self.Who's own explicit form already could; a bare recursive call is the
  same resolution gap, just aimed at the CURRENT method's own name instead
  of a sibling's. }
procedure TA.Test3;
begin
  writeln('Test3 (bare recursive, explicit Self stops it):');
  if False then Test3;
  writeln('  -> TA.Test3 (no infinite recursion)');
end;

var
  C: TC;
begin
  C.Init;
  C.Test1;
  C.Test2;
  C.Test3;
end.

(*
CHECK:Test1 (bare, no parens):
CHECK-NEXT:  -> TC.Who
CHECK-NEXT:Test2 (bare, with parens):
CHECK-NEXT:  -> TC.Who
CHECK-NEXT:Test3 (bare recursive, explicit Self stops it):
CHECK-NEXT:  -> TA.Test3 (no infinite recursion)
*)
