(*
Issue #696: FindFirst(path, <integer-literal>, sr) -- passing an integer
LITERAL directly as the attribute-mask second argument, rather than a named
constant (dos-findfirst-findnext-findclose-iterate-a-real-directory.pas's
own AnyFile) or a typed variable, used to crash the compiler:
CGExprCore.cpp's IntLitExpr case always materializes an integer literal as
i64, unconditionally of context, and CGProcCall.cpp's own findfirst arm
passed that straight through with no coercion to plang_dos_findfirst's
`uint16_t attr` parameter -- an LLVM IR verifier failure ("i64 63 passed to
an i16 parameter") on the single most idiomatic call form real Turbo
Pascal code uses (`FindFirst('*.*', AnyFile, sr)` worked, since a named
constant/variable already carried the right narrower LLVM type by the time
it reached codegen; `FindFirst('*.*', $3F, sr)` did not).  Same fixture
setup and SET-membership checks as that sibling test, just with $3F written
directly rather than through AnyFile.

RUN: rm -rf %t.dir && mkdir -p %t.dir
RUN: printf 'aa' > %t.dir/one.txt
RUN: printf 'bbb' > %t.dir/two.txt
RUN: printf 'c' > %t.dir/three.dat
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir | FileCheck %s
*)

program DosFindFirstIntegerLiteralAttr;
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
  FindFirst(Dir + '/*.txt', $3F, F);
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
