(*
A label and a variable sharing a name fall through to the same generic
duplicate-declaration diagnostic every other kind of name collision in this
block gets (Symtab.define's per-scope check, Sema.cpp) -- there is no
label-specific wording, and none is needed: "duplicate declaration of
'done'" already says which name and doesn't need to also say which two kinds
collided.  -std=turbo here only so the label declaration itself is legal
(see identifier-label-is-rejected-under-iso7185-and-extended-pascal.pas,
test/Sema/SemaGoto, for that separate, unrelated diagnostic) and does not
mask this one.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: duplicate declaration of 'done'
*)

program p; label done; var done : integer; begin end.
