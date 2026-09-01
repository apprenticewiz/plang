(*
Issue #723 (a gap in #616's own fix, PR #722): the bare-form 'inherited;'
signature check compared IsVar/IsUntyped/type per parameter, but never
IsConst.  For a STRUCTURED (record/array/set) parameter, IsConst changes
the LLVM ABI shape (isStructuredForConstByRef, CodeGenProcs.cpp): a
'const r: rec' parameter is passed by pointer, a plain 'r: rec' parameter
is passed by LLVM struct value.  TD.Show here merely STATICALLY HIDES
TA.Show (no 'virtual' override relationship enforced between the two
signatures) and differs from it ONLY in the 'r' parameter's const-ness --
previously unchecked, this crashed CodeGen with an LLVM IR verifier
failure (a raw struct value forwarded into the ancestor's pointer
parameter) instead of giving a clean diagnostic, exactly the bug class
issue #616 was filed to close.  Confirmed against a local fpc -Mtp build:
fpc rejects this same program too ("Wrong number of parameters specified
for call to 'Show'" / "Found declaration: Show(const rec)") -- bare
'inherited' requires an exact signature match, const-ness included, not
merely a call-compatible one.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BareInheritedConstMismatch;

type
  rec = record a, b: Integer end;
  TA = object
    procedure Show(const r: rec); virtual;
  end;
  TD = object(TA)
    procedure Show(r: rec);
  end;

procedure TA.Show(const r: rec);
begin
  writeln('TA.Show ', r.a, ' ', r.b)
end;

procedure TD.Show(r: rec);
begin
  inherited;
  writeln('TD.Show done')
end;

var d: TD; v: rec;
begin
  v.a := 7; v.b := 9;
  d.Show(v);
  writeln(v.a, ' ', v.b)
end.

(*
CHECK: error: bare 'inherited' in 'TD.Show' cannot forward its own parameters to ancestor method 'TA.Show': the signatures do not match
*)
