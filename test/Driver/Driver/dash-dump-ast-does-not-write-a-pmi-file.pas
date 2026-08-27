(*
Issue #274: -dump-ast is documented (and, via -dump-tokens/-dump-parse-tree,
implemented elsewhere) as a read-only inspection mode, but the front end's
module pipeline wrote every module body's .pmi file unconditionally, before
ever checking whether DumpAst had been requested -- so "plang -dump-ast" on
a file that declares a module mutated the source directory as a side
effect, even though the other two dump modes already returned before doing
any such thing.

Also checks that an ordinary (non-dump) compile of the same module still
writes its .pmi file -- the fix reorders the DumpAst check ahead of
writePMIFiles, it does not remove the write.

The .pmi filename is lowercased (issues #168/#173/#175): module names are
case-insensitive, so "m.pmi" is what a module declared "M" actually
produces, not "M.pmi".
*)

(*
RUN: rm -rf %t.dir
RUN: split-file %s %t.dir

RUN: %plang -std=iso10206 -dump-ast %t.dir/mod.pas
RUN: test ! -e %t.dir/m.pmi

RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: test -e %t.dir/m.pmi
*)

//--- mod.pas
module M interface;
export M = (F);
function F: integer;
end.
module M;
function F;
begin F := 1 end;
end.
