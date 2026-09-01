(*
Tier 3 Cluster C item 5's own dialect split (`file of char` is a genuine
binary file under Turbo, but ISO 7185 section 6.4.3.5 gives it no separate
identity from `text` at all) already has two independently-written
siblings pinning each half on its own
(test/CodeGen/Turbo/file-of-char-is-a-genuine-binary-file-not-text.pas,
test/CodeGen/TextOnlyProcedures/a-file-of-char-is-still-text-under-iso-
and-ep-not-turbo.pas). This puts both halves in ONE lit file, side by
side, the shape test/Driver/Turbo/the-formatting-matrix-from-one-shared-
source-under-iso7185-and-turbo.pas already establishes for exactly this
kind of dialect-reversal proof.

NOT byte-identical source, unlike that formatting-matrix precedent --
confirmed while writing this test, not assumed: real Turbo Pascal retired
its own 2-argument `Rewrite(f, 'name')` name-binding form (Cluster A item
4 -- the second argument now means an untyped file's RecSize, an Integer,
and Sema rejects a string there under -std=turbo; see
reset-and-rewrite-refuse-a-non-string-second-argument-under-turbo.pas and
reset-rewrite-string-literal-filename-still-works-under-iso7185.pas's own
comment on the retirement), so the two variants below open the file the
two dialects' own real idioms actually use -- `Assign`+`Rewrite(f)` for
Turbo, `Rewrite(f, 'name')` for ISO 7185 -- and are identical in every
other respect (the two `write(f, ch)` calls and the `close(f)` that
follows, the actual mechanism under test).

Two characters written through `write(f, ch)` are DELIBERATELY not enough
to tell "2 raw bytes" from "2 characters through the text path" by
content alone -- what actually distinguishes them is close()'s own
behavior: the text path appends a trailing newline to finish an
unterminated line; Turbo's raw binary path does not. So this pins the
exact byte COUNT (`wc -c`, portable -- NOT `od --strict-whitespace
--match-full-lines`, which the file-of-char-is-a-genuine-binary-file
sibling's own comment already flags as a real, previously-hit GNU-vs-BSD
`od` column-padding failure on macOS CI) as the real proof: 2 bytes under
Turbo, 3 (the trailing newline) under ISO 7185.

RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/turbo.pas -o %t.turbo
RUN: %run %t.turbo
RUN: wc -c < file-of-char-binary-under-turbo-text-under-iso7185-turbo.dat | tr -d ' ' | FileCheck --check-prefix=TURBO-SIZE %s

RUN: %plang -std=iso7185 %t.dir/iso.pas -o %t.iso
RUN: %run %t.iso
RUN: wc -c < file-of-char-binary-under-turbo-text-under-iso7185-iso.dat | tr -d ' ' | FileCheck --check-prefix=ISO-SIZE %s
*)

(*
TURBO-SIZE:2
ISO-SIZE:3
*)

//--- turbo.pas
var f: file of char;
begin
  assign(f, 'file-of-char-binary-under-turbo-text-under-iso7185-turbo.dat');
  rewrite(f);
  write(f, 'A');
  write(f, 'B');
  close(f);
end.

//--- iso.pas
program p(output);
var f: file of char;
begin
  rewrite(f, 'file-of-char-binary-under-turbo-text-under-iso7185-iso.dat');
  write(f, 'A');
  write(f, 'B');
  close(f);
end.
