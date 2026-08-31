(*
Turbo Tier 4, Cluster C item 6: Dos.FindFirst/FindNext/FindClose iterate a
real directory via opendir(3)/readdir(3)/closedir(3) (runtime/
plang_dos.cpp's own plang_dos_findfirst/plang_dos_findnext/
plang_dos_findclose).  The RUN line creates three real fixture files in a
scratch directory; the program iterates them with a real wildcard pattern
and reports which of the three real names it actually found (order is not
guaranteed by readdir(3), so this checks SET membership via three separate
booleans rather than a fixed order) plus that every Size matches the real
file it was written with.

RUN: rm -rf %t.dir && mkdir -p %t.dir
RUN: printf 'aa' > %t.dir/one.txt
RUN: printf 'bbb' > %t.dir/two.txt
RUN: printf 'c' > %t.dir/three.dat
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir | FileCheck %s
*)

program DosFindFirstFindNext;
uses Dos;
var
  F: SearchRec;
  Dir: string;
  SawOne, SawTwo, SizeOk: Boolean;
  Count: Integer;
begin
  Dir := ParamStr(1);
  SawOne := False;
  SawTwo := False;
  SizeOk := True;
  Count := 0;
  FindFirst(Dir + '/*.txt', faAnyFile, F);
  while DosError = 0 do
  begin
    Count := Count + 1;
    if F.Name = 'one.txt' then
    begin
      SawOne := True;
      if F.Size <> 2 then SizeOk := False;
    end;
    if F.Name = 'two.txt' then
    begin
      SawTwo := True;
      if F.Size <> 3 then SizeOk := False;
    end;
    FindNext(F);
  end;
  FindClose(F);
  Writeln('count: ', Count);
  Writeln('saw-one: ', SawOne);
  Writeln('saw-two: ', SawTwo);
  Writeln('size-ok: ', SizeOk);
end.

(*
CHECK:count: 2
CHECK-NEXT:saw-one: TRUE
CHECK-NEXT:saw-two: TRUE
CHECK-NEXT:size-ok: TRUE
*)
