(*
RUN: split-file %s %t.dir
RUN: not %plang %t.dir/new-wrong-type.pas -o %t.a 2> %t.a.err
RUN: FileCheck --check-prefix=TYPE %s < %t.a.err
RUN: not %plang %t.dir/new-too-many.pas -o %t.b 2> %t.b.err
RUN: FileCheck --check-prefix=EXTRA %s < %t.b.err
RUN: not %plang %t.dir/dispose-wrong-type.pas -o %t.c 2> %t.c.err
RUN: FileCheck --check-prefix=TYPE %s < %t.c.err
*)

(*
TYPE: not compatible with variant tag type 'boolean'
EXTRA: more case-constants than
*)

(*
ISO Sec6.6.5.3: new/dispose's extra arguments, for a record with a variant
part, are its case-constants -- one per nested variant level actually
selected, each a value of that specific level's own tag type.  Extra
arguments were type-checked only for a SCHEMA's discriminants; a variant
record's tag arguments were entirely unchecked for both count and type, and
dispose had no argument checking of any kind, schema or variant.
*)

//--- new-wrong-type.pas
program p(output);
type
  r = record case b: boolean of true: (x: integer); false: (y: integer) end;
var q: ^r;
begin new(q, 5) end.

//--- new-too-many.pas
program p(output);
type
  r = record case b: boolean of true: (x: integer); false: (y: integer) end;
var q: ^r;
begin new(q, true, false, true) end.

//--- dispose-wrong-type.pas
program p(output);
type
  r = record case b: boolean of true: (x: integer); false: (y: integer) end;
var q: ^r;
begin new(q, true); dispose(q, 42) end.
