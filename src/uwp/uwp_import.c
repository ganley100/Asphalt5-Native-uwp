/* First-launch game file setup for the UWP / Xbox build.
 *
 * The package never contains the game.  On first launch the user's own
 * files are taken from the first place that has them:
 *
 *   1. LocalState (uploaded with the Xbox Device Portal file explorer)
 *   2. <install>\game\ (a package built locally with the files bundled)
 *   3. <drive>:\Asphalt5\ on a USB drive (D: to K:)
 *
 * and laid out in LocalState exactly like the Windows build expects:
 *
 *   engine\apk\lib\armeabi-v7a\libasphalt5.so, engine\apk\res, engine\apk\assets
 *   gamefiles\GloftA5HD\...                     (unless GloftA5HD\ is already there)
 *
 * The APK is unpacked with miniz.  LocalState is also where the game writes
 * its saves, so the data folder has to live there.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "third_party/miniz/miniz.h"
#include "uwp_host.h"

#define SO_ENTRY "lib/armeabi-v7a/libasphalt5.so"
#define SO_MARKER L"engine\\apk\\lib\\armeabi-v7a\\libasphalt5.so"

static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* A GloftA5HD folder is usable when it has the general package chunks. */
static int data_dir_ok(const wchar_t *dir)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    _snwprintf(pat, MAX_PATH * 2 - 1, L"%ls\\package_general.bar_*.cnk", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    return 1;
}

static void mkdirs(const wchar_t *path)
{
    wchar_t tmp[MAX_PATH * 2];
    wcsncpy(tmp, path, MAX_PATH * 2 - 1);
    tmp[MAX_PATH * 2 - 1] = 0;
    for (wchar_t *p = tmp; *p; p++) {
        if ((*p == L'\\' || *p == L'/') && p > tmp && p[-1] != L':') {
            wchar_t c = *p;
            *p = 0;
            CreateDirectoryW(tmp, NULL);
            *p = c;
        }
    }
    CreateDirectoryW(tmp, NULL);
}

/* ---- APK ---------------------------------------------------------------- */
static int wanted_entry(const char *name)
{
    if (!strcmp(name, SO_ENTRY)) return 1;
    if (!strncmp(name, "res/", 4) || !strncmp(name, "assets/", 7)) return 1;
    return 0;
}

static int safe_entry(const char *name)
{
    if (!*name || name[0] == '/' || name[0] == '\\' || strchr(name, ':')) return 0;
    for (const char *p = name; *p; ) {
        const char *e = p;
        while (*e && *e != '/' && *e != '\\') e++;
        if (e - p == 2 && p[0] == '.' && p[1] == '.') return 0;
        p = *e ? e + 1 : e;
    }
    return 1;
}

static int find_apk(const wchar_t *dir, wchar_t *out)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    _snwprintf(pat, MAX_PATH * 2 - 1, L"%ls\\*.apk", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    _snwprintf(out, MAX_PATH * 2 - 1, L"%ls\\%ls", dir, fd.cFileName);
    out[MAX_PATH * 2 - 1] = 0;
    return 1;
}

static const char *extract_apk(const wchar_t *apk, const wchar_t *local, float p0, float p1)
{
    FILE *f = _wfopen(apk, L"rb");
    if (!f) return "The APK could not be opened.";
    _fseeki64(f, 0, SEEK_END);
    mz_uint64 size = (mz_uint64)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_cfile(&zip, f, size, 0)) {
        fclose(f);
        return "The APK is not a valid zip archive.";
    }
    if (mz_zip_reader_locate_file(&zip, SO_ENTRY, NULL, 0) < 0) {
        mz_zip_reader_end(&zip);
        fclose(f);
        return "This APK has no lib/armeabi-v7a/libasphalt5.so. The Xbox build is "
               "compiled from the Asphalt 5 HD 3.2.4 (Sony Ericsson) translation; "
               "use that APK.";
    }
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    const char *err = NULL;
    for (mz_uint i = 0; i < n && !err; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        if (!wanted_entry(st.m_filename) || !safe_entry(st.m_filename)) continue;
        wchar_t rel[MAX_PATH], dst[MAX_PATH * 2];
        int wn = MultiByteToWideChar(CP_UTF8, 0, st.m_filename, -1, rel, MAX_PATH);
        if (wn <= 0) continue;
        for (wchar_t *q = rel; *q; q++) if (*q == L'/') *q = L'\\';
        _snwprintf(dst, MAX_PATH * 2 - 1, L"%ls\\engine\\apk\\%ls", local, rel);
        dst[MAX_PATH * 2 - 1] = 0;
        wchar_t *slash = wcsrchr(dst, L'\\');
        if (slash) { *slash = 0; mkdirs(dst); *slash = L'\\'; }
        FILE *o = _wfopen(dst, L"wb");
        if (!o) { err = "Could not write to the app's LocalState folder."; break; }
        if (!mz_zip_reader_extract_to_cfile(&zip, i, o, 0)) err = "The APK is damaged.";
        fclose(o);
        if ((i & 15) == 0) uwp_progress(p0 + (p1 - p0) * (float)i / (float)(n ? n : 1));
    }
    mz_zip_reader_end(&zip);
    fclose(f);
    return err;
}

/* ---- data folder copy -------------------------------------------------- */
typedef struct { unsigned total, done; float p0, p1; } CopyProgress;

static unsigned count_files(const wchar_t *dir)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    unsigned n = 0;
    _snwprintf(pat, MAX_PATH * 2 - 1, L"%ls\\*", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wchar_t sub[MAX_PATH * 2];
            _snwprintf(sub, MAX_PATH * 2 - 1, L"%ls\\%ls", dir, fd.cFileName);
            sub[MAX_PATH * 2 - 1] = 0;
            n += count_files(sub);
        } else {
            n++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static int copy_tree(const wchar_t *src, const wchar_t *dst, CopyProgress *pr)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    mkdirs(dst);
    _snwprintf(pat, MAX_PATH * 2 - 1, L"%ls\\*", src);
    pat[MAX_PATH * 2 - 1] = 0;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 1;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t s[MAX_PATH * 2], d[MAX_PATH * 2];
        _snwprintf(s, MAX_PATH * 2 - 1, L"%ls\\%ls", src, fd.cFileName);
        _snwprintf(d, MAX_PATH * 2 - 1, L"%ls\\%ls", dst, fd.cFileName);
        s[MAX_PATH * 2 - 1] = d[MAX_PATH * 2 - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = copy_tree(s, d, pr) && ok;
        } else {
            if (FAILED(CopyFile2(s, d, NULL))) {
                fprintf(stderr, "[import] copy failed %ls (%lu)\n", s, GetLastError());
                ok = 0;
            }
            pr->done++;
            if ((pr->done & 7) == 0 || pr->done == pr->total)
                uwp_progress(pr->p0 + (pr->p1 - pr->p0) * (float)pr->done /
                                          (float)(pr->total ? pr->total : 1));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

/* ---- entry ------------------------------------------------------------- */
static int local_data_ok(const wchar_t *local)
{
    wchar_t d[MAX_PATH * 2];
    _snwprintf(d, MAX_PATH * 2 - 1, L"%ls\\gamefiles\\GloftA5HD", local);
    d[MAX_PATH * 2 - 1] = 0;
    if (data_dir_ok(d)) return 1;
    _snwprintf(d, MAX_PATH * 2 - 1, L"%ls\\GloftA5HD", local);
    return data_dir_ok(d);
}

const char *uwp_import_game_files(const wchar_t *local, const wchar_t *install)
{
    wchar_t sources[12][MAX_PATH];
    int ns = 0;
    wchar_t marker[MAX_PATH * 2];
    _snwprintf(marker, MAX_PATH * 2 - 1, L"%ls\\%ls", local, SO_MARKER);
    marker[MAX_PATH * 2 - 1] = 0;
    int have_so = file_exists(marker);
    int have_data = local_data_ok(local);
    fprintf(stderr, "[import] engine %s, data %s\n", have_so ? "present" : "missing",
            have_data ? "present" : "missing");
    if (have_so && have_data) return NULL;

    wcsncpy(sources[ns++], local, MAX_PATH - 1);
    if (install && *install) {
        _snwprintf(sources[ns], MAX_PATH - 1, L"%ls\\game", install);
        sources[ns++][MAX_PATH - 1] = 0;
    }
    for (wchar_t drv = L'D'; drv <= L'K' && ns < 12; drv++) {
        _snwprintf(sources[ns], MAX_PATH - 1, L"%lc:\\Asphalt5", drv);
        sources[ns][MAX_PATH - 1] = 0;
        if (dir_exists(sources[ns])) ns++;
    }

    if (!have_so) {
        const char *err = NULL;
        for (int i = 0; i < ns && !have_so; i++) {
            wchar_t apk[MAX_PATH * 2];
            if (!find_apk(sources[i], apk)) continue;
            fprintf(stderr, "[import] extracting %ls\n", apk);
            uwp_progress(0.0f);
            err = extract_apk(apk, local, 0.0f, 0.3f);
            if (!err) have_so = file_exists(marker);
        }
        if (err) return err;
    }

    if (!have_data) {
        for (int i = 0; i < ns && !have_data; i++) {
            static const wchar_t *subs[] = { L"GloftA5HD", L"gamefiles\\GloftA5HD" };
            for (int k = 0; k < 2 && !have_data; k++) {
                wchar_t src[MAX_PATH * 2], dst[MAX_PATH * 2];
                _snwprintf(src, MAX_PATH * 2 - 1, L"%ls\\%ls", sources[i], subs[k]);
                src[MAX_PATH * 2 - 1] = 0;
                if (!data_dir_ok(src)) continue;
                _snwprintf(dst, MAX_PATH * 2 - 1, L"%ls\\gamefiles\\GloftA5HD", local);
                dst[MAX_PATH * 2 - 1] = 0;
                if (!_wcsicmp(src, dst)) { have_data = 1; break; }
                fprintf(stderr, "[import] copying %ls\n", src);
                CopyProgress pr = { count_files(src), 0, 0.3f, 1.0f };
                uwp_progress(0.3f);
                if (!copy_tree(src, dst, &pr))
                    return "Some game data files could not be copied. Check the free "
                           "space on the console and try again.";
                have_data = local_data_ok(local);
            }
        }
    }

    if (have_so && have_data) {
        uwp_progress(1.0f);
        return NULL;
    }
    if (!have_so)
        return "Your Asphalt 5 HD 3.2.4 APK was not found.\n\n"
               "Put the .apk file and the GloftA5HD data folder in a folder named Asphalt5 "
               "on a USB drive (for example E:\\Asphalt5), or upload them to this app's "
               "LocalState folder with the Xbox Device Portal. Then start the game again.";
    return "The GloftA5HD game data folder was not found.\n\n"
           "Put the GloftA5HD folder (it contains package_general.bar_*.cnk) next to the "
           "APK in the Asphalt5 folder on a USB drive, or upload it to this app's "
           "LocalState folder with the Xbox Device Portal. Then start the game again.";
}
