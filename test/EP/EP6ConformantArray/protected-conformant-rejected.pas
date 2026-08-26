(*
RUN: not %plang_ep %s -o %t
*)

program p;
procedure tryWrite(protected A: array [lo..hi : integer] of integer);
begin A[lo] := 99 end;
var arr: array [1..3] of integer;
begin tryWrite(arr) end.
