(*
Issue #697: nested/interleaved FindFirst/FindNext loops used to corrupt
each other's state, because runtime/plang_dos.cpp kept the search
directory in one process-wide global (g_LastFindDir) shared by every
SearchRec instead of per-instance -- starting an inner FindFirst on a
different directory redirected the OUTER, still-in-progress FindNext
loop's candidate paths into the inner directory, so outer `stat` calls
failed and outer entries were silently skipped.  This is the canonical
recursive-directory-traversal pattern (an outer loop over a directory's
entries, with an inner FindFirst/FindNext loop over a subdirectory inside
the outer loop's body), so this test drives exactly that shape: an outer
loop over 5 *.txt files in one directory, with a nested FindFirst/FindNext
loop over 2 *.txt files in a SEPARATE subdirectory running to completion
on every single outer iteration.

RUN: rm -rf %t.dir && mkdir -p %t.dir/sub
RUN: printf 'a' > %t.dir/o1.txt
RUN: printf 'a' > %t.dir/o2.txt
RUN: printf 'a' > %t.dir/o3.txt
RUN: printf 'a' > %t.dir/o4.txt
RUN: printf 'a' > %t.dir/o5.txt
RUN: printf 'a' > %t.dir/sub/i1.txt
RUN: printf 'a' > %t.dir/sub/i2.txt
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir | FileCheck %s
*)

program DosNestedFindFirstFindNext;
uses Dos;
var
  Outer, Inner: SearchRec;
  Dir: string;
  OuterCount, InnerCountSum: Integer;
begin
  Dir := ParamStr(1);
  OuterCount := 0;
  InnerCountSum := 0;
  FindFirst(Dir + '/*.txt', AnyFile, Outer);
  while DosError = 0 do
  begin
    OuterCount := OuterCount + 1;
    FindFirst(Dir + '/sub/*.txt', AnyFile, Inner);
    while DosError = 0 do
    begin
      InnerCountSum := InnerCountSum + 1;
      FindNext(Inner);
    end;
    FindClose(Inner);
    FindNext(Outer);
  end;
  FindClose(Outer);
  Writeln('outer-count: ', OuterCount);
  Writeln('inner-count-sum: ', InnerCountSum);
end.

(*
CHECK:outer-count: 5
CHECK-NEXT:inner-count-sum: 10
*)
