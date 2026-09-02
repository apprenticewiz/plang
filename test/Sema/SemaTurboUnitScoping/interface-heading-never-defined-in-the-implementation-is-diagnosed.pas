(*
Issue #698: an interface heading with no matching implementation body at all
(as opposed to a mismatched one, see the sibling
implementation-heading-mismatching-its-interface-is-diagnosed.pas) used to
compile clean -- the heading simply never got a definition anywhere, and
only linking a caller against this unit's object file would reveal the
missing symbol.  checkUnitImplConformance / checkUnit's own
!Unit.ImplementationBlock fallback (Sema.cpp) now diagnoses this at the
unit's own compile, matching fpc's "Forward declaration not solved".
Exercises both a unit with an (otherwise empty) implementation section and
one with no implementation declarations at all, just an init body.

RUN: split-file %s %t.dir
RUN: not %plang -std=turbo -c %t.dir/never_defined.pas -o %t.dir/never_defined.o 2> %t.dir/never_defined.err
RUN: FileCheck --check-prefix=EMPTY-IMPL %s < %t.dir/never_defined.err
RUN: not %plang -std=turbo -c %t.dir/no_impl_decls.pas -o %t.dir/no_impl_decls.o 2> %t.dir/no_impl_decls.err
RUN: FileCheck --check-prefix=NO-IMPL-BLOCK %s < %t.dir/no_impl_decls.err
*)

//--- never_defined.pas
unit NeverDefinedUnit;

interface

procedure Foo(X: Integer);

implementation

end.

(*
EMPTY-IMPL: error: 'Foo' is declared in this unit's interface but is never given a defining declaration in the implementation
*)

//--- no_impl_decls.pas
unit NoImplDeclsUnit;

interface

procedure Foo(X: Integer);

implementation

begin
end.

(*
NO-IMPL-BLOCK: error: 'Foo' is declared in this unit's interface but is never given a defining declaration in the implementation
*)
