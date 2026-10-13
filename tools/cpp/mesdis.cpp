// mesdis — D.C.4 .mes disassembler (verified command-stream model).
// Usage:
//   mesdis <file.mes>          full disassembly with labels, decoded text
//   mesdis <file.mes> text     message text only
//   mesdis <dir>               all .mes in a directory (summary)
#include "dc4/mesfile.h"
#include "dc4/messtream.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace dc4;

#ifdef _WIN32
static std::string sjis_to_utf8(const std::string& s) {
    if (s.empty()) return s;
    int wlen = MultiByteToWideChar(932, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(wlen), 0);
    MultiByteToWideChar(932, 0, s.c_str(), int(s.size()), &w[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    std::string u(size_t(ulen), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, &u[0], ulen, nullptr, nullptr);
    return u;
}
#else
static std::string sjis_to_utf8(const std::string& s) { return s; }
#endif

static std::set<uint32_t> g_labels;

static const char* kind_name(CmdKind k) {
    switch (k) {
    case CmdKind::Nop: return "nop";
    case CmdKind::Param2: return "cmd";
    case CmdKind::ParamStr: return "cmd";
    case CmdKind::Str: return "str";
    case CmdKind::Text: return "TEXT";
    case CmdKind::Words4: return "cmd";
    }
    return "?";
}

static void disassemble(const MesFile& mes, const char* path, bool text_only) {
    printf("; %s  version=0x%06X labels=%zu\n", path, mes.version, mes.labels.size());
    g_labels.clear();
    for (auto& l : mes.labels) g_labels.insert(l.offset);
    MesStream st(mes);
    MesCommand c;
    while (st.next(&c)) {
        bool is_label = g_labels.count(c.pos) != 0;
        if (text_only && c.kind != CmdKind::Text) continue;
        if (is_label) printf("L%u:\n", c.pos);
        switch (c.kind) {
        case CmdKind::Nop:
            printf("%6u  nop\n", c.pos);
            break;
        case CmdKind::Param2:
            printf("%6u  %02X %u, %u\n", c.pos, c.op, c.p1, c.p2);
            break;
        case CmdKind::ParamStr:
            printf("%6u  %02X %u, \"%s\"\n", c.pos, c.op, c.p1, c.str.c_str());
            break;
        case CmdKind::Str:
            printf("%6u  %02X \"%s\"\n", c.pos, c.op, c.str.c_str());
            break;
        case CmdKind::Text:
            printf("%6u  TEXT[%02X] \"%s\"\n", c.pos, c.op, sjis_to_utf8(c.str).c_str());
            break;
        case CmdKind::Words4:
            printf("%6u  %02X %u, %u, %u, %u\n", c.pos, c.op,
                   c.words[0], c.words[1], c.words[2], c.words[3]);
            break;
        }
    }
}

static void disassemble_file_summary(const std::string& path) {
    MesFile mes;
    std::string err;
    if (!mes.load_file(path, &err)) {
        printf("FAIL %s: %s\n", path.c_str(), err.c_str());
        return;
    }
    MesStream st(mes);
    MesCommand c;
    size_t cmds = 0, texts = 0;
    while (st.next(&c)) {
        ++cmds;
        if (c.kind == CmdKind::Text) ++texts;
    }
    printf("%-40s version=0x%06X labels=%zu cmds=%zu textlines=%zu\n",
           path.c_str(), mes.version, mes.labels.size(), cmds, texts);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: mesdis <file.mes> [text]\n       mesdis <dir>  (summary)\n");
        return 1;
    }
    std::string a = argv[1];
    bool text_only = argc > 2 && std::string(argv[2]) == "text";
    if (a.size() > 4 && a.compare(a.size() - 4, 4, ".mes") == 0) {
        MesFile mes;
        std::string err;
        if (!mes.load_file(a, &err)) {
            printf("FAIL %s: %s\n", a.c_str(), err.c_str());
            return 1;
        }
        disassemble(mes, a.c_str(), text_only);
        return 0;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((a + "\\*.mes").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("no .mes files in %s\n", a.c_str());
        return 1;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            disassemble_file_summary(a + "\\" + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}
