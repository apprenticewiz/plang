/// plang_strings.cpp — Turbo Tier 4, Cluster C item 7: the real bodies
/// behind the shipped `Strings` unit (share/plang/units/Strings.pas).
///
/// See that file's own header comment for the full account of why these
/// functions live here rather than in Strings.pas' own (deliberately empty)
/// implementation section, and exactly how each one is wired to the mangled
/// symbol a caller's `uses Strings` reaches: CGLinkage's own scheme
/// (PlangProcPrefix + moduleScope("strings") + the call's own spelling of
/// the name, "pas_strings$Name") is not something an ordinary C++ function
/// name can spell, so every entry point below binds to its real link name
/// through GCC/Clang's `asm("...")` label extension instead of relying on
/// its own C++ name.
///
/// Every function here operates on a PChar exactly the way this project's
/// own Codegen already represents one -- a plain `ptr` (i8*), null-
/// terminated, with no length prefix and no capacity tracked anywhere
/// (CGBinaryOps.cpp/CGIndexAccess.cpp's own PChar arithmetic is already
/// raw pointer arithmetic at the LLVM level) -- so the real C standard
/// library's own null-terminated-string functions are exact, ABI-
/// compatible matches for most of these, used directly rather than
/// reimplemented by hand.  A plain `char*`/`const char*` is byte-for-byte
/// what a PChar argument or result already is; nothing here needs any
/// conversion at the boundary.
///
/// StrNew/StrDispose reuse this project's own Tier 3 GetMem/FreeMem
/// entry points (plang_tp_getmem/plang_tp_freemem, runtime/plang_sys.cpp)
/// directly, rather than std::malloc/std::free, so a pointer StrNew hands
/// back is indistinguishable from one a Pascal `GetMem` call produced --
/// interchangeable with an explicit FreeMem the same size class the
/// project's other heap accounting already assumes.
///
/// StrPCopy/StrPLCopy's `Source` is a `var string` (see Strings.pas' own
/// header comment for why, forced by this exact extern-call boundary, not
/// stylistic), which arrives here as a raw pointer to this project's own
/// ShortString layout: byte 0 is the length, bytes 1.. are the character
/// data, NOT null-terminated (plang_sstr.cpp's own header comment is the
/// one description of this layout — reused here rather than duplicated).

#include <cctype>
#include <cstdint>
#include <cstring>

#include <strings.h> // strcasecmp(3) -- StrIComp's case-insensitive compare

namespace plang {

extern "C" {

// ---- Tier 3's own heap, reused rather than duplicated (plang_sys.cpp) ----
void *plang_tp_getmem(int64_t Size);
void  plang_tp_freemem(void *P, int64_t Size);

// ---- StrLen ----------------------------------------------------------------
uint32_t plang_strings_StrLen(const char *Str) asm("pas_strings$StrLen");
uint32_t plang_strings_StrLen(const char *Str) {
    return static_cast<uint32_t>(std::strlen(Str));
}

// ---- StrCopy ----------------------------------------------------------------
char *plang_strings_StrCopy(char *Dest, const char *Source) asm("pas_strings$StrCopy");
char *plang_strings_StrCopy(char *Dest, const char *Source) {
    std::strcpy(Dest, Source);
    return Dest;
}

// ---- StrLCopy: copies at most MaxLen characters, always null-terminates
// the result (real Borland/FPC field practice -- unlike strncpy(3), which
// does NOT null-terminate a source at least MaxLen long, and DOES pad with
// NULs when Source is shorter; neither behavior is what StrLCopy documents,
// so this is hand-rolled rather than a strncpy wrapper). ----------------------
char *plang_strings_StrLCopy(char *Dest, const char *Source, uint32_t MaxLen)
    asm("pas_strings$StrLCopy");
char *plang_strings_StrLCopy(char *Dest, const char *Source, uint32_t MaxLen) {
    uint32_t I = 0;
    while (I < MaxLen && Source[I] != '\0') { Dest[I] = Source[I]; ++I; }
    Dest[I] = '\0';
    return Dest;
}

// ---- StrCat ----------------------------------------------------------------
char *plang_strings_StrCat(char *Dest, const char *Source) asm("pas_strings$StrCat");
char *plang_strings_StrCat(char *Dest, const char *Source) {
    std::strcat(Dest, Source);
    return Dest;
}

// ---- StrLCat: appends at most MaxLen TOTAL length in Dest, matching real
// Borland/FPC (MaxLen bounds the RESULT length, not just the appended
// suffix, unlike strncat(3), whose third argument bounds only the number of
// source bytes appended regardless of Dest's existing length). --------------
char *plang_strings_StrLCat(char *Dest, const char *Source, uint32_t MaxLen)
    asm("pas_strings$StrLCat");
char *plang_strings_StrLCat(char *Dest, const char *Source, uint32_t MaxLen) {
    const uint32_t DestLen = static_cast<uint32_t>(std::strlen(Dest));
    if (DestLen >= MaxLen) return Dest; // already at or past the limit: no-op
    uint32_t I = DestLen;
    uint32_t J = 0;
    while (I < MaxLen && Source[J] != '\0') { Dest[I] = Source[J]; ++I; ++J; }
    Dest[I] = '\0';
    return Dest;
}

// ---- StrComp/StrLComp/StrIComp: strcmp(3)'s own convention (negative/zero/
// positive), confirmed as real Borland/FPC field practice too -- Str1 < Str2,
// Str1 = Str2, Str1 > Str2 respectively, not just a -1/0/1 tri-state. -------
int16_t plang_strings_StrComp(const char *Str1, const char *Str2) asm("pas_strings$StrComp");
int16_t plang_strings_StrComp(const char *Str1, const char *Str2) {
    return static_cast<int16_t>(std::strcmp(Str1, Str2));
}

int16_t plang_strings_StrLComp(const char *Str1, const char *Str2, uint32_t MaxLen)
    asm("pas_strings$StrLComp");
int16_t plang_strings_StrLComp(const char *Str1, const char *Str2, uint32_t MaxLen) {
    return static_cast<int16_t>(std::strncmp(Str1, Str2, MaxLen));
}

int16_t plang_strings_StrIComp(const char *Str1, const char *Str2) asm("pas_strings$StrIComp");
int16_t plang_strings_StrIComp(const char *Str1, const char *Str2) {
    return static_cast<int16_t>(::strcasecmp(Str1, Str2));
}

// ---- StrPos: substring search; nil (not found) when Str2 is not a
// substring of Str1 -- an empty Str2 always matches at Str1 (real Borland/
// FPC field practice, and also strstr(3)'s own). -----------------------------
char *plang_strings_StrPos(const char *Str1, const char *Str2) asm("pas_strings$StrPos");
char *plang_strings_StrPos(const char *Str1, const char *Str2) {
    return const_cast<char *>(std::strstr(Str1, Str2));
}

// ---- StrScan/StrRScan: first/last occurrence of Chr, nil if absent.  A
// search for the NUL terminator itself (Chr = #0) is real Borland/FPC field
// practice too (strchr(3)/strrchr(3) already both honor it: the terminator
// counts as part of the string being searched). -----------------------------
char *plang_strings_StrScan(const char *Str, char Chr) asm("pas_strings$StrScan");
char *plang_strings_StrScan(const char *Str, char Chr) {
    return const_cast<char *>(std::strchr(Str, static_cast<unsigned char>(Chr)));
}

char *plang_strings_StrRScan(const char *Str, char Chr) asm("pas_strings$StrRScan");
char *plang_strings_StrRScan(const char *Str, char Chr) {
    return const_cast<char *>(std::strrchr(Str, static_cast<unsigned char>(Chr)));
}

// ---- StrUpper/StrLower: in-place, returns the same pointer it was given. --
char *plang_strings_StrUpper(char *Str) asm("pas_strings$StrUpper");
char *plang_strings_StrUpper(char *Str) {
    for (char *P = Str; *P; ++P)
        *P = static_cast<char>(std::toupper(static_cast<unsigned char>(*P)));
    return Str;
}

char *plang_strings_StrLower(char *Str) asm("pas_strings$StrLower");
char *plang_strings_StrLower(char *Str) {
    for (char *P = Str; *P; ++P)
        *P = static_cast<char>(std::tolower(static_cast<unsigned char>(*P)));
    return Str;
}

// ---- StrNew/StrDispose: heap-allocate (via this project's own Tier 3
// GetMem) a copy of Str, sized to its real length + 1 for the terminator;
// nil in, nil out (real Borland/FPC field practice: StrNew(nil) = nil,
// StrDispose(nil) is a no-op), matching plang_tp_freemem's own nil-safety
// (std::free(nullptr) is always defined). ------------------------------------
char *plang_strings_StrNew(const char *Str) asm("pas_strings$StrNew");
char *plang_strings_StrNew(const char *Str) {
    if (!Str) return nullptr;
    const auto Len = static_cast<int64_t>(std::strlen(Str));
    auto *P = static_cast<char *>(plang_tp_getmem(Len + 1));
    if (!P) return nullptr;
    std::memcpy(P, Str, static_cast<std::size_t>(Len) + 1);
    return P;
}

void plang_strings_StrDispose(char *Str) asm("pas_strings$StrDispose");
void plang_strings_StrDispose(char *Str) {
    if (!Str) return;
    const auto Len = static_cast<int64_t>(std::strlen(Str));
    plang_tp_freemem(Str, Len + 1);
}

// ---- StrPCopy/StrPLCopy: Pascal ShortString -> PChar.  Source is `var
// string` (Strings.pas' own header comment explains why), so it arrives as
// a raw pointer to this project's own ShortString layout: byte 0 is the
// length, bytes 1.. are the character data (plang_sstr.cpp's own header
// comment). StrPCopy copies the whole thing; StrPLCopy truncates to MaxLen
// the same way StrLCopy does above -- both always null-terminate. ----------
char *plang_strings_StrPCopy(char *Dest, const void *Source) asm("pas_strings$StrPCopy");
char *plang_strings_StrPCopy(char *Dest, const void *Source) {
    const auto *S   = static_cast<const uint8_t *>(Source);
    const uint8_t Len = S[0];
    std::memcpy(Dest, S + 1, Len);
    Dest[Len] = '\0';
    return Dest;
}

char *plang_strings_StrPLCopy(char *Dest, const void *Source, uint32_t MaxLen)
    asm("pas_strings$StrPLCopy");
char *plang_strings_StrPLCopy(char *Dest, const void *Source, uint32_t MaxLen) {
    const auto *S   = static_cast<const uint8_t *>(Source);
    uint32_t Len = S[0];
    if (Len > MaxLen) Len = MaxLen;
    std::memcpy(Dest, S + 1, Len);
    Dest[Len] = '\0';
    return Dest;
}

} // extern "C"

} // namespace plang
