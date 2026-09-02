(*
Issue #581: share/plang/units/Dos.pas used to export the file-attribute
bitmask constants under Delphi/SysUtils-style prefixed names (faReadOnly,
faDirectory, ...) instead of real TP7's own unprefixed names (ReadOnly,
Directory, ...) -- confirmed against `fpc -Mtp`'s own Unix Dos unit
(rtl/inc/dosh.inc).  This blocked the documented TP7 FindFirst
attribute-mask idiom (`FindFirst('*.*', Directory, F)`).  This test checks
both that the real, unprefixed names are usable as a FindFirst mask AND
that their values are the standard DOS attribute bits.

RUN: rm -rf %t.dir && mkdir -p %t.dir/subdir
RUN: printf 'x' > %t.dir/plain.txt
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir | FileCheck %s
*)

program DosAttrConstantsUnprefixed;
uses Dos;
var
  F: SearchRec;
  Dir: string;
  SawSubdir: Boolean;
begin
  Dir := ParamStr(1);
  SawSubdir := False;
  FindFirst(Dir + '/*', Directory, F);
  while DosError = 0 do
  begin
    if (F.Name = 'subdir') and (F.Attr and Directory <> 0) then
      SawSubdir := True;
    FindNext(F);
  end;
  FindClose(F);
  Writeln('saw-subdir: ', SawSubdir);
  Writeln('ReadOnly=', ReadOnly, ' Hidden=', Hidden, ' SysFile=', SysFile,
          ' VolumeID=', VolumeID, ' Directory=', Directory, ' Archive=', Archive,
          ' AnyFile=', AnyFile);
end.

(*
CHECK:saw-subdir: TRUE
CHECK-NEXT:ReadOnly=1 Hidden=2 SysFile=4 VolumeID=8 Directory=16 Archive=32 AnyFile=63
*)
