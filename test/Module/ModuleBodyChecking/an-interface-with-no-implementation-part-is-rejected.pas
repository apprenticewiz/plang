(*
EP §6.11.1: a module interface part promises a body, and the two are
ordinarily written in the same file (module-declaration lets them come in
either order, never one alone).  This file writes only "module Foo
interface; ... end." and never a matching "module Foo; ... end." anywhere
-- until now that compiled with exit code 0 and no diagnostic at all,
leaving whatever used Foo later to hit a missing .pmi or a confusing
undefined __plang_init_foo at link time instead of a clear error at the
point the real mistake was made.

RUN: not %plang -std=iso10206 -c %s -o %t.o 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: module 'Foo' has an interface part but no matching implementation part
*)

module Foo interface;
export Foo = (Bar);
const Bar = 1;
end.
