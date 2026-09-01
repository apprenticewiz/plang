(*
Issue #616: a bare 'inherited;' forwards THIS activation's own actual
parameter values, by position, straight into the resolved ancestor's own
parameter list -- well-typed only when the two signatures are IDENTICAL.
A true virtual override already guarantees that
(err_object_virtual_override_signature_mismatch), but TD.Init here merely
STATICALLY HIDES TA.Init (no 'virtual' on either constructor -- real Turbo
Pascal object-model constructors are never virtual, confirmed by
virtual-constructor-is-refused.pas), and the two signatures genuinely
differ (Integer vs string) -- previously unchecked, this crashed CodeGen
with an LLVM IR verifier failure (the string argument forwarded into the
i16 ancestor parameter) instead of giving a clean diagnostic.  Confirmed
against a local fpc -Mtp build: even though both sides take exactly ONE
parameter, fpc rejects this too ("Wrong number of parameters specified for
call to 'Init'") -- bare 'inherited' requires an exact signature match, not
merely a call-compatible one.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BareInheritedSignatureMismatch;

type
  TA = object
    constructor Init(v: Integer);
  end;
  TD = object(TA)
    constructor Init(n: string);
  end;

constructor TA.Init(v: Integer);
begin
end;

constructor TD.Init(n: string);
begin
  inherited;
end;

begin
end.

(*
CHECK: error: bare 'inherited' in 'TD.Init' cannot forward its own parameters to ancestor method 'TA.Init': the signatures do not match
*)
