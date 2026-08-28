(*
The same regression gate the sibling dollar-brace/dollar-i comment tests
give the earlier directive categories, for this task's two new ones:
under -std=iso7185 and -std=iso10206, the R switch (a real one under
Turbo) and the APPTYPE directive (a real accept-and-ignore one under
Turbo) are still nothing but ordinary brace comments below -- no
diagnostic, no change in behavior, and in particular the out-of-range
index is still governed only by ISO 7185's own range-checking default
(on), never by Turbo's switch meaning, which does not exist here at all.

NOTE: this comment deliberately never spells the two-character opener and
closer next to real directive text together, and never writes a lone
brace at all -- doing either inside plang's own top-of-file documentation
comment would close THIS comment early under ISO 7185's "either
terminator closes either" rule, the same trap
iso7185-and-extended-pascal-still-treat-dollar-brace-as-an-ordinary-comment.pas
already documents.
*)

(*
RUN: %plang %s -o %t.iso7185
RUN: not %run %t.iso7185 2> %t.iso7185.err
RUN: FileCheck --check-prefix=ABORTS %s < %t.iso7185.err

RUN: %plang -std=iso10206 %s -o %t.ep
RUN: not %run %t.ep 2> %t.ep.err
RUN: FileCheck --check-prefix=ABORTS %s < %t.ep.err
*)

(*
ABORTS: array index 10 out of bounds 1..3
*)

program dollarbraceswitches;
{$R+}
{$APPTYPE CONSOLE}
var a: array[1..3] of integer;
    i: integer;
begin
  i := 10;
  a[i] := 1
end.
