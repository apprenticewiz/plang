/// plang_dos.cpp — the real runtime behind share/plang/units/Dos.pas
/// (Turbo Tier 4, Cluster C item 6).  POSIX only (Linux/macOS) -- see that
/// unit's own header comment for the full account of how a call in a
/// program that 'uses Dos' reaches the functions below, and why some are
/// bound directly by a linker-symbol alias (asm(...)) and others are
/// reached through CodeGen's own small Dos-intrinsic recognizer instead
/// (CGProcCall.cpp / CGFuncCall.cpp, "Dos-unit intrinsics").
///
/// Kept to plain C-library calls throughout, no STL containers -- the
/// runtime is linked into generated programs without the C++ standard
/// library (see plang_sys.cpp's own comment on the identical constraint).

#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// An asm-label needs an explicit leading underscore on Mach-O (macOS): the
// compiler adds that prefix automatically for an ordinary, non-asm-labelled
// C symbol, but NOT on top of an explicit asm(...) label -- confirmed the
// hard way (this project's sibling Strings/Printer units linked fine on
// Linux/ELF and failed with "Undefined symbols ... _pas_..." on macOS/
// Mach-O before this fix, since CGLinkage's own LLVM-emitted call site
// already expects the Mach-O convention but a literal, unprefixed asm-label
// does not supply it).
#if defined(__APPLE__)
#define PLANG_ASM_NAME(name) asm("_" name)
#else
#define PLANG_ASM_NAME(name) asm(name)
#endif

namespace plang {

namespace {

/// A ShortString(255) (Turbo `string`) in memory -- plang_sstr.cpp's own
/// layout, reproduced here so this file can read/write one directly: a
/// one-byte length prefix (0..255, never more: the field is one byte
/// wide), then the raw character data, packed, never NUL-terminated.
struct PlangSStr255 {
    uint8_t len;
    char    data[255];
} __attribute__((packed));
static_assert(sizeof(PlangSStr255) == 256);

/// share/plang/units/Dos.pas's own SearchRec, field for field, in
/// declaration order -- this item's own report worked out this exact
/// layout empirically (dumping the LLVM IR plang's own CodeGen emits for
/// `var F: SearchRec`) rather than assuming it, and this struct's offsets
/// are checked against that dump below.  DirHandle/SearchAttr/PathPrefix
/// are this implementation's own private iteration state (Dos.pas's own
/// header comment); Attr/Time/Size/Name are the public, documented fields.
struct PlangDosSearchRec {
    void*        dirHandle;
    uint16_t     searchAttr;
    PlangSStr255 pathPrefix;
    uint8_t      attr;
    int32_t      time;
    int32_t      size;
    PlangSStr255 name;
};
static_assert(offsetof(PlangDosSearchRec, dirHandle)  == 0);
static_assert(offsetof(PlangDosSearchRec, searchAttr) == 8);
static_assert(offsetof(PlangDosSearchRec, pathPrefix) == 10);
static_assert(offsetof(PlangDosSearchRec, attr)       == 266);
static_assert(offsetof(PlangDosSearchRec, time)       == 268);
static_assert(offsetof(PlangDosSearchRec, size)       == 272);
static_assert(offsetof(PlangDosSearchRec, name)       == 276);
static_assert(sizeof(PlangDosSearchRec) == 536);

/// Dos.pas's own DateTime record: six Word fields, Year first -- no padding
/// between them (all the same 2-byte width/alignment), confirmed the same
/// empirical way as PlangDosSearchRec above.
struct PlangDosDateTime {
    uint16_t Year, Month, Day, Hour, Min, Sec;
};
static_assert(sizeof(PlangDosDateTime) == 12);

/// share/plang/units/Dos.pas's own file-attribute bits (real Borland-
/// documented values, this unit's own header comment).
constexpr uint16_t FaReadOnly  = 0x01;
constexpr uint16_t FaHidden    = 0x02;
constexpr uint16_t FaDirectory = 0x10;
constexpr uint16_t FaArchive   = 0x20;

/// Writes \p Src (a NUL-terminated C string, or nullptr for "") into the
/// ShortString at \p Dest, truncating at \p DestCap -- the same truncating
/// convention every OTHER ShortString-producing runtime entry point in
/// this compiler already uses (plang_sstr.cpp's own header comment).
void writeSStr(PlangSStr255* Dest, int64_t DestCap, const char* Src) {
    if (DestCap > 255) DestCap = 255;
    if (DestCap < 0)   DestCap = 0;
    std::size_t n = Src ? std::strlen(Src) : 0;
    if (static_cast<int64_t>(n) > DestCap) n = static_cast<std::size_t>(DestCap);
    Dest->len = static_cast<uint8_t>(n);
    if (n) std::memcpy(Dest->data, Src, n);
}

/// A NUL-terminated copy of a ShortString's own bytes, for handing to a
/// POSIX call that wants a plain C string -- a ShortString has no
/// terminator of its own (plang_sstr.cpp's own layout comment).  Returns a
/// pointer into a small pool of static buffers cycled round-robin, so two
/// arguments to the SAME call (Exec's Path and ComLine, say) do not
/// overwrite one another before both are read -- Pascal programs run
/// single-threaded, the same assumption plang_time.cpp's own static
/// buffers already make.
const char* cstr(const PlangSStr255* S) {
    constexpr int    Slots = 4;
    static char      Bufs[Slots][256];
    static int        Next = 0;
    char* buf = Bufs[Next];
    Next = (Next + 1) % Slots;
    std::memcpy(buf, S->data, S->len);
    buf[S->len] = '\0';
    return buf;
}

/// The DOS-standard 32-bit packed date/time format PackTime/UnpackTime and
/// SearchRec's own Time field all share: bits 0-4 seconds/2, 5-10 minutes,
/// 11-15 hours, 16-20 day, 21-24 month, 25-31 year-1980.  Pure bit
/// arithmetic, no calendar math -- real Borland/FPC field practice
/// (rtl/inc/dosh.inc's own PackTime/UnpackTime documented contract).
int32_t packDosTime(int year, int month, int day, int hour, int min, int sec) {
    int y = year - 1980;
    if (y < 0) y = 0;
    if (y > 127) y = 127;
    return static_cast<int32_t>(
        ((y & 0x7F) << 25) | ((month & 0xF) << 21) | ((day & 0x1F) << 16) |
        ((hour & 0x1F) << 11) | ((min & 0x3F) << 5) | ((sec / 2) & 0x1F));
}

void unpackDosTime(int32_t p, int* year, int* month, int* day, int* hour,
                    int* min, int* sec) {
    uint32_t u = static_cast<uint32_t>(p);
    *year  = 1980 + static_cast<int>((u >> 25) & 0x7F);
    *month = static_cast<int>((u >> 21) & 0xF);
    *day   = static_cast<int>((u >> 16) & 0x1F);
    *hour  = static_cast<int>((u >> 11) & 0x1F);
    *min   = static_cast<int>((u >> 5) & 0x3F);
    *sec   = static_cast<int>((u & 0x1F) * 2);
}

/// This implementation's own last-child-exit-code storage for Exec/
/// DosExitCode -- real TP/FPC field practice keeps this as a global too
/// (FPC's own LastDosExitCode); this project keeps it `static` here
/// instead, reached only through DosExitCode(), rather than exporting a
/// second predefined variable Dos.pas's own interface never declares.
int16_t g_LastDosExitCode = 0;

/// The directory component FindFirst opened, kept for FindNext to
/// reconstruct each candidate's full path with -- global rather than a
/// SearchRec field because this project's own SearchRec (Dos.pas) has no
/// field budget left for it, and real Turbo/FPC programs never run
/// FindFirst/FindNext concurrently on two different directories from two
/// different threads, the identical single-threaded assumption
/// plang_time.cpp's own static buffers already make.
char g_LastFindDir[1024] = "";

} // namespace

/// Dos.pas's own `var DosError: Integer` -- real storage, defined here and
/// declared `external` by every OTHER compiled object that reads or writes
/// it (Dos.pas's own header comment on DosError; CGLinkage::mangledGlobal's
/// existing convention, unchanged, is what computes the exact name below on
/// the Pascal side).  Turbo's Integer is 16 bits (this project's own
/// Turbo-milestone decision), so this is int16_t, not int.
extern "C" int16_t plang_dos_doserror PLANG_ASM_NAME("pasg_dos$DosError");
int16_t plang_dos_doserror = 0;

extern "C" {

// ---- Info/Date/Time -- scalar-only, bound directly by mangled-name alias.
// See Dos.pas's own header comment for why these do not need CodeGen's
// Dos-intrinsic recognizer at all: every parameter here is a Word (a var
// pointer or a plain i16), which lowers identically whether the caller was
// compiled by plang or by a real C++ compiler.

void plang_dos_getdate(uint16_t* year, uint16_t* month, uint16_t* day,
                        uint16_t* dayOfWeek) PLANG_ASM_NAME("pas_dos$GetDate");
void plang_dos_getdate(uint16_t* year, uint16_t* month, uint16_t* day,
                        uint16_t* dayOfWeek) {
    std::time_t now = std::time(nullptr);
    std::tm*    tm  = std::localtime(&now);
    if (!tm) { *year = 1980; *month = *day = 1; *dayOfWeek = 0; return; }
    *year      = static_cast<uint16_t>(1900 + tm->tm_year);
    *month     = static_cast<uint16_t>(1 + tm->tm_mon);
    *day       = static_cast<uint16_t>(tm->tm_mday);
    *dayOfWeek = static_cast<uint16_t>(tm->tm_wday); // 0=Sunday, real TP convention
}

void plang_dos_gettime(uint16_t* hour, uint16_t* minute, uint16_t* second,
                        uint16_t* sec100) PLANG_ASM_NAME("pas_dos$GetTime");
void plang_dos_gettime(uint16_t* hour, uint16_t* minute, uint16_t* second,
                        uint16_t* sec100) {
    // Sec100 (hundredths of a second): std::time's own second granularity
    // has no such thing, so -- rather than always reporting 0 -- this reads
    // a finer-grained clock, the same choice Randomize's own runtime entry
    // point already made (clock_gettime(CLOCK_REALTIME, ...)), for a real
    // sub-second value.
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    std::time_t now = ts.tv_sec;
    std::tm*    tm  = std::localtime(&now);
    if (!tm) { *hour = *minute = *second = *sec100 = 0; return; }
    *hour   = static_cast<uint16_t>(tm->tm_hour);
    *minute = static_cast<uint16_t>(tm->tm_min);
    *second = static_cast<uint16_t>(tm->tm_sec);
    *sec100 = static_cast<uint16_t>(ts.tv_nsec / 10000000L);
}

void plang_dos_setdate(uint16_t year, uint16_t month, uint16_t day)
    PLANG_ASM_NAME("pas_dos$SetDate");
void plang_dos_setdate(uint16_t year, uint16_t month, uint16_t day) {
    // Real `fpc -Mtp` field practice (rtl/unix/dos.pp's own SetDate):
    // rebuild a full local time from the CURRENT time-of-day plus the new
    // date, and hand it to settimeofday(2) -- WITHOUT checking whether the
    // call succeeded.  Confirmed empirically against that source: a
    // permission failure (the overwhelmingly common case running as a
    // non-root user) is silently absorbed, not reported through DosError
    // and not raised as an error -- reproduced here exactly, deliberately,
    // rather than inventing an error-reporting contract real Turbo/FPC do
    // not have.
    std::time_t now = std::time(nullptr);
    std::tm     tmv  = *std::localtime(&now);
    tmv.tm_year = year - 1900;
    tmv.tm_mon  = month - 1;
    tmv.tm_mday = day;
    std::time_t t = std::mktime(&tmv);
    if (t == static_cast<std::time_t>(-1)) return;
    struct timeval tv { t, 0 };
    (void)settimeofday(&tv, nullptr); // result deliberately ignored, see above
}

void plang_dos_settime(uint16_t hour, uint16_t minute, uint16_t second,
                        uint16_t sec100) PLANG_ASM_NAME("pas_dos$SetTime");
void plang_dos_settime(uint16_t hour, uint16_t minute, uint16_t second,
                        uint16_t sec100) {
    std::time_t now = std::time(nullptr);
    std::tm     tmv  = *std::localtime(&now);
    tmv.tm_hour = hour;
    tmv.tm_min  = minute;
    tmv.tm_sec  = second;
    std::time_t t = std::mktime(&tmv);
    if (t == static_cast<std::time_t>(-1)) return;
    struct timeval tv { t, static_cast<suseconds_t>(sec100) * 10000 };
    (void)settimeofday(&tv, nullptr); // silent on failure -- see SetDate above
}

void plang_dos_packtime(const PlangDosDateTime* t, int32_t* p)
    PLANG_ASM_NAME("pas_dos$PackTime");
void plang_dos_packtime(const PlangDosDateTime* t, int32_t* p) {
    *p = packDosTime(t->Year, t->Month, t->Day, t->Hour, t->Min, t->Sec);
}

void plang_dos_unpacktime(int32_t p, PlangDosDateTime* t)
    PLANG_ASM_NAME("pas_dos$UnpackTime");
void plang_dos_unpacktime(int32_t p, PlangDosDateTime* t) {
    int y, mo, d, h, mi, s;
    unpackDosTime(p, &y, &mo, &d, &h, &mi, &s);
    t->Year = static_cast<uint16_t>(y);
    t->Month = static_cast<uint16_t>(mo);
    t->Day = static_cast<uint16_t>(d);
    t->Hour = static_cast<uint16_t>(h);
    t->Min = static_cast<uint16_t>(mi);
    t->Sec = static_cast<uint16_t>(s);
}

uint16_t plang_dos_dosexitcode(void) PLANG_ASM_NAME("pas_dos$DosExitCode");
uint16_t plang_dos_dosexitcode(void) {
    return static_cast<uint16_t>(g_LastDosExitCode);
}

// ---- Disk -- Drive is a real DOS drive-letter index on real Borland/FPC,
// meaningless on POSIX.  Real FPC Unix field practice (rtl/unix/dos.pp's
// own DiskFree/DiskSize): Drive=0 means the current working directory's
// own filesystem, queried via statvfs(2) (fpStatFS there); every other
// value this implementation ALSO treats as "current" (Dos.pas's own header
// comment explains why AddDisk's per-drive registry is out of scope: it is
// unreachable, since this project never implements AddDisk itself).

int64_t plang_dos_diskfree(uint8_t) PLANG_ASM_NAME("pas_dos$DiskFree");
int64_t plang_dos_diskfree(uint8_t) {
    struct statvfs sv {};
    if (statvfs(".", &sv) != 0) return -1;
    return static_cast<int64_t>(sv.f_bavail) * static_cast<int64_t>(sv.f_frsize);
}

int64_t plang_dos_disksize(uint8_t) PLANG_ASM_NAME("pas_dos$DiskSize");
int64_t plang_dos_disksize(uint8_t) {
    struct statvfs sv {};
    if (statvfs(".", &sv) != 0) return -1;
    return static_cast<int64_t>(sv.f_blocks) * static_cast<int64_t>(sv.f_frsize);
}

// ---- GetDir -- Drive gets the identical POSIX reinterpretation as
// DiskFree/DiskSize (Dos.pas's own header comment): every value reports
// the real current working directory.

void plang_dos_getdir(uint8_t, PlangSStr255* dir) PLANG_ASM_NAME("pas_dos$GetDir");
void plang_dos_getdir(uint8_t, PlangSStr255* dir) {
    char buf[256];
    if (getcwd(buf, sizeof(buf)))
        writeSStr(dir, 255, buf);
    else
        writeSStr(dir, 255, "");
}

// ---- FindNext/FindClose -- scalar (a single ptr) parameter, so these two
// also bind directly; FindFirst itself cannot (its own Path is a `string`
// VALUE parameter) and is reached through CodeGen's own Dos-intrinsic
// recognizer's plang_dos_findfirst below instead.

// Plain `static` (not another nested anonymous namespace) for these two:
// this whole block sits inside the `extern "C" { ... }` opened above, and a
// linkage-specification block gives every name inside it C language
// linkage regardless of an unmangled anonymous-namespace name -- `static`
// is the unambiguous way to keep these two internal to this translation
// unit rather than exported as plain, collision-prone symbols "attrMatches"
// and "statInto".

/// The one attribute test FindFirst/FindNext share: real FPC field
/// practice (rtl/unix/dos.pp's own FindGetFileInfo) -- a candidate is kept
/// only when it has no attribute bit OUTSIDE the caller's own search mask
/// (readonly/archive files are always "plain", so FindFirst always adds
/// both to the mask before this ever runs).
static bool attrMatches(uint16_t fileAttr, uint16_t searchMask) {
    return (fileAttr & static_cast<uint16_t>(~searchMask)) == 0;
}

/// Computes this implementation's own portable subset of the real
/// attribute bits (Directory/Hidden; ReadOnly from access(2); SysFile/
/// VolumeID/Archive are not meaningful on POSIX and are never set, the
/// same real FPC field practice its own GetFAttr/FindGetFileInfo follow)
/// for \p fullPath, and its size/mtime, into \p rec.  Returns false if
/// stat(2) itself fails.
static bool statInto(const char* fullPath, const char* nameOnly, PlangDosSearchRec* rec) {
    struct stat st {};
    if (::stat(fullPath, &st) != 0) return false;
    uint16_t attr = 0;
    if (S_ISDIR(st.st_mode)) attr |= FaDirectory;
    if (nameOnly[0] == '.') attr |= FaHidden;
    if (::access(fullPath, W_OK) != 0) attr |= FaReadOnly;
    rec->attr = static_cast<uint8_t>(attr);
    rec->size = static_cast<int32_t>(st.st_size);
    std::tm* tm = std::localtime(&st.st_mtime);
    rec->time = tm ? packDosTime(1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday,
                                  tm->tm_hour, tm->tm_min, tm->tm_sec)
                    : 0;
    writeSStr(&rec->name, 255, nameOnly);
    return true;
}

void plang_dos_findnext(PlangDosSearchRec* f) PLANG_ASM_NAME("pas_dos$FindNext");
void plang_dos_findnext(PlangDosSearchRec* f) {
    DIR* d = static_cast<DIR*>(f->dirHandle);
    if (!d) { plang_dos_doserror = 18; return; }
    // pathPrefix carries the search pattern's own basename half (set by
    // plang_dos_findfirst below); the directory component itself lives in
    // g_LastFindDir, and the directory is already open.
    const char* pattern = cstr(&f->pathPrefix);
    for (;;) {
        struct dirent* de = readdir(d);
        if (!de) { plang_dos_doserror = 18; return; }
        if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0)
            continue;
        if (fnmatch(pattern, de->d_name, 0) != 0) continue;
        char full[1280];
        std::snprintf(full, sizeof(full), "%s%s", g_LastFindDir, de->d_name);
        if (statInto(full, de->d_name, f) && attrMatches(f->attr, f->searchAttr)) {
            plang_dos_doserror = 0;
            return;
        }
    }
}

void plang_dos_findclose(PlangDosSearchRec* f) PLANG_ASM_NAME("pas_dos$FindClose");
void plang_dos_findclose(PlangDosSearchRec* f) {
    if (f->dirHandle) closedir(static_cast<DIR*>(f->dirHandle));
    f->dirHandle = nullptr;
}

} // extern "C"

// ---- Dos-intrinsic entry points -- reached only through CodeGen's own
// recognizer (CGProcCall.cpp/CGFuncCall.cpp), never bound by symbol alias:
// every one of these has a `string` VALUE parameter or result somewhere in
// its real signature, which -- confirmed empirically, Dos.pas's own header
// comment -- does not cross the C++/Pascal boundary correctly via a plain
// alias.  Plain, scalar/pointer-only signatures of this implementation's
// own choosing, called explicitly by name from CodeGen instead.

extern "C" void plang_dos_getenv(const char* name, PlangSStr255* dest,
                                  int64_t destCap) {
    const char* v = name ? std::getenv(name) : nullptr;
    writeSStr(dest, destCap, v ? v : "");
}

extern "C" void plang_dos_chdir(const char* path) {
    plang_dos_doserror = (::chdir(path) == 0) ? 0 : 3;
}

extern "C" void plang_dos_mkdir(const char* path) {
    plang_dos_doserror = (::mkdir(path, 0777) == 0) ? 0 : 3;
}

extern "C" void plang_dos_rmdir(const char* path) {
    plang_dos_doserror = (::rmdir(path) == 0) ? 0 : 3;
}

extern "C" void plang_dos_findfirst(const char* path, uint16_t attr,
                                     PlangDosSearchRec* f) {
    std::memset(f, 0, sizeof(*f));
    if (!path || !path[0]) { plang_dos_doserror = 3; return; }
    const char* slash = std::strrchr(path, '/');
    const char* pattern = slash ? slash + 1 : path;
    if (slash) {
        std::size_t dirLen = static_cast<std::size_t>(slash - path) + 1;
        if (dirLen >= sizeof(g_LastFindDir)) dirLen = sizeof(g_LastFindDir) - 1;
        std::memcpy(g_LastFindDir, path, dirLen);
        g_LastFindDir[dirLen] = '\0';
    } else {
        std::strcpy(g_LastFindDir, "./");
    }
    writeSStr(&f->pathPrefix, 255, pattern);
    f->searchAttr = static_cast<uint16_t>(attr | FaArchive | FaReadOnly);
    const char* dirToOpen = slash ? g_LastFindDir : ".";
    DIR* d = opendir(dirToOpen);
    if (!d) { plang_dos_doserror = 3; return; }
    f->dirHandle = d;
    plang_dos_findnext(f);
}

extern "C" void plang_dos_exec(const char* path, const char* comLine) {
    g_LastDosExitCode = 0;
    if (!path || !path[0]) { plang_dos_doserror = 2; return; }
    // ComLine split on whitespace into the child's own argv, with Path
    // substituted for argv[0] -- real FPC field practice (rtl/unix/dos.pp's
    // own Exec, via StringToPPChar); no quoting support, matching that same
    // real-world limitation.  A fixed small argv: real command lines this
    // unit is ever handed in practice fit comfortably.
    constexpr int MaxArgs = 32;
    char  buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", comLine ? comLine : "");
    char* argv[MaxArgs + 1];
    int   argc = 0;
    argv[argc++] = const_cast<char*>(path);
    char* save = nullptr;
    for (char* tok = strtok_r(buf, " \t", &save);
         tok && argc < MaxArgs; tok = strtok_r(nullptr, " \t", &save))
        argv[argc++] = tok;
    argv[argc] = nullptr;

    pid_t pid = fork();
    if (pid < 0) { plang_dos_doserror = 8; return; }
    if (pid == 0) {
        execv(path, argv);
        _exit(127); // real FPC field practice: a failed exec exits 127
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { plang_dos_doserror = 8; return; }
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
    g_LastDosExitCode = static_cast<int16_t>(code);
    plang_dos_doserror = (code != 127) ? 0 : 8;
}

} // namespace plang
