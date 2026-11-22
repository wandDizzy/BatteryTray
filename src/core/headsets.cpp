// Wireless headsets from SteelSeries, Corsair and HyperX, and SteelSeries wireless mice.
//
// All of them answer a short request written as an OUTPUT report on a vendor collection
// with an INPUT report. Each family is one table entry: which collection to use, which
// requests to send and how to read the answer. Byte positions are in "hidapi" indexing
// (a leading report ID of 0 is removed; non-zero report IDs stay at [0]).
//
// Protocol facts were reverse engineered by the HeadsetControl project
// (https://github.com/Sapd/HeadsetControl) and, for SteelSeries mice, by rivalcfg
// (https://github.com/flozz/rivalcfg); this is an independent implementation.
#include "devices.h"
#include "hid_util.h"
#include <algorithm>
#include <map>

namespace {

using Bytes = std::vector<unsigned char>;

struct Step {
    Bytes req;
    int   acceptAt = -1;      // optional check: resp[acceptAt] == acceptVal
    unsigned char acceptVal = 0;
    int   minLen = 4;
    int   timeoutMs = 500;
};

struct Result {
    bool present = true;      // false = headset not connected to its base / receiver
    int  battery = -1;
    bool charging = false;
};

struct HsSpec {
    const wchar_t* name;
    DevType type;
    unsigned short vid;
    std::vector<unsigned short> pids;
    int iface;                         // -1 = any
    unsigned short page, usage;        // 0 = any vendor collection
    std::vector<Step> steps;
    Result (*parse)(unsigned short pid, const std::vector<Bytes>& r);
    int minIntervalSec = 0;            // ask the device at most this often (keeps slow receivers happy)
    const char* source = "headset";
};

int Map(int v, int lo, int hi) {
    if (hi <= lo) return v;
    int p = (v - lo) * 100 / (hi - lo);
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

// ------------------------------------------------------------------ SteelSeries

Result ArctisSimple(unsigned short, const std::vector<Bytes>& r) {   // Arctis 1 / 7X / 7P / 7 / Pro
    Result x;
    x.battery = std::min<int>(r[0][2], 100);
    return x;
}

Result Arctis9(unsigned short, const std::vector<Bytes>& r) {
    Result x;
    x.battery = Map(r[0][3], 0x64, 0x9A);
    x.charging = r[0][4] == 0x01;
    return x;
}

Result ArctisNova7(unsigned short pid, const std::vector<Bytes>& r) {
    static const unsigned short discrete[] = {0x2202, 0x2206, 0x220a, 0x223a, 0x227a, 0x22a4};
    const Bytes& d = r[0];
    Result x;
    if (d[3] == 0x00) { x.present = false; return x; }
    x.charging = d[3] == 0x01 || d[3] == 0x02;
    bool isDiscrete = std::find(std::begin(discrete), std::end(discrete), pid) != std::end(discrete);
    x.battery = isDiscrete ? Map(d[2], 0, 4) : std::min<int>(d[2], 100);
    return x;
}

Result ArctisNova5(unsigned short, const std::vector<Bytes>& r) {   // Nova 5 / 5X / 3P / 3X
    const Bytes& d = r[0];
    Result x;
    if (d[1] == 0x02) { x.present = false; return x; }
    x.charging = d[4] == 0x01;
    x.battery = std::min<int>(d[3], 100);
    return x;
}

Result Arctis7Plus(unsigned short, const std::vector<Bytes>& r) {
    const Bytes& d = r[0];
    Result x;
    if (d[1] == 0x01) { x.present = false; return x; }
    x.charging = d[3] == 0x01;
    x.battery = Map(d[2], 0, 4);
    return x;
}

Result ArctisNovaPro(unsigned short, const std::vector<Bytes>& r) {
    const Bytes& d = r[0];
    Result x;
    if (d.size() < 16) { x.present = false; return x; }
    if (d[15] == 0x01) { x.present = false; return x; }
    x.charging = d[15] == 0x02;
    x.battery = Map(d[6], 0, 8);
    return x;
}

// ------------------------------------------------------------------ Corsair

Result CorsairVoid(unsigned short, const std::vector<Bytes>& r) {
    const Bytes& d = r[0];
    Result x;
    unsigned char st = d[4];
    if (st == 0) { x.present = false; return x; }          // disconnected / wired model
    x.charging = st == 4 || st == 5;
    x.battery = std::min<int>(d[2] & 0x7F, 100);            // bit 7 = microphone up
    return x;
}

// ------------------------------------------------------------------ HyperX

Result HyperXCloud2(unsigned short, const std::vector<Bytes>& r) {
    Result x;
    x.battery = std::min<int>(r[0][7], 100);
    x.charging = r.size() > 1 && r[1].size() > 4 && r[1][4] == 1;
    return x;
}

Result HyperXAlpha(unsigned short, const std::vector<Bytes>& r) {
    Result x;
    if (r[0][3] == 0x01) { x.present = false; return x; }   // connection status: 1 = not connected
    x.charging = r[1][3] == 0x01;
    x.battery = std::min<int>(r[2][3], 100);
    return x;
}

// ------------------------------------------------------------------ SteelSeries mice (rivalcfg protocol facts)

Result SsMouseAerox(unsigned short, const std::vector<Bytes>& r) {   // Aerox 3/5/9 Wireless, Prime Wireless
    Result x;
    int raw = r[0][1];
    int level = ((raw & 0x7F) - 1) * 5;
    if (raw == 0 || level < 0 || level > 100) { x.present = false; return x; }   // mouse off
    x.battery = level;
    x.charging = (raw & 0x80) != 0;
    return x;
}

Result SsMouseRival(unsigned short, const std::vector<Bytes>& r) {   // Rival 3 Wireless, Rival 650
    Result x;
    int level = r[0][0];
    if (level > 100) { x.present = false; return x; }
    x.battery = level;
    x.charging = r[0][2] != 0;
    return x;
}

Bytes Pad(Bytes b, size_t n) { b.resize(n, 0); return b; }

const std::vector<HsSpec>& Specs() {
    static const std::vector<HsSpec> v = {
        // SteelSeries (0x1038)
        {L"SteelSeries Arctis", DevType::Headset, 0x1038, {0x12b3, 0x12b6, 0x12d7, 0x12d5}, 3, 0xFF43, 0x0202,
         {{{0x06, 0x12}}}, ArctisSimple},
        {L"SteelSeries Arctis 7", DevType::Headset, 0x1038, {0x1260, 0x12ad, 0x1252, 0x1280}, 5, 0xFF43, 0x0202,
         {{{0x06, 0x18}}}, ArctisSimple},
        {L"SteelSeries Arctis 9", DevType::Headset, 0x1038, {0x12c2, 0x12c4}, 0, 0, 0, {{{0x00, 0x20}, -1, 0, 5}}, Arctis9},
        {L"SteelSeries Arctis Nova 7", DevType::Headset, 0x1038,
         {0x2202, 0x22A1, 0x227e, 0x2206, 0x2258, 0x229e, 0x22ad, 0x223a, 0x22a9, 0x227a, 0x22a4, 0x22a5}, 3, 0xFFC0, 0x0001,
         {{{0x00, 0xb0}, 0, 0xb0, 4}}, ArctisNova7},
        {L"SteelSeries Arctis Nova 5", DevType::Headset, 0x1038, {0x2232, 0x2253, 0x2269, 0x226d}, 3, 0xFFC0, 0x0001,
         {{{0x00, 0xb0}, 0, 0xb0, 5}}, ArctisNova5},
        {L"SteelSeries Arctis 7+", DevType::Headset, 0x1038, {0x220e, 0x2212, 0x2216, 0x2236}, 3, 0xFFC0, 0x0001,
         {{{0x00, 0xb0}, 0, 0xb0, 4}}, Arctis7Plus},
        {L"SteelSeries Arctis Nova Pro Wireless", DevType::Headset, 0x1038, {0x12e0, 0x12e5}, 4, 0, 0,
         {{Pad({0x06, 0xb0}, 31), -1, 0, 16}}, ArctisNovaPro},
        // SteelSeries wireless mice: cable (0x92) and 2.4 GHz receiver (0x92 | 0x40)
        {L"SteelSeries Mouse", DevType::Mouse, 0x1038, {0x183A, 0x187A, 0x1854, 0x185E, 0x1862, 0x185A, 0x1876, 0x1842, 0x184A},
         3, 0, 0, {{{0x00, 0x92}, -1, 0, 2}}, SsMouseAerox},
        {L"SteelSeries Mouse", DevType::Mouse, 0x1038, {0x1838, 0x1878, 0x1852, 0x185C, 0x1860, 0x1858, 0x1874, 0x1840, 0x1848},
         3, 0, 0, {{{0x00, 0xD2}, -1, 0, 2}}, SsMouseAerox},
        {L"SteelSeries Rival", DevType::Mouse, 0x1038, {0x1830, 0x1872, 0x1726, 0x172B}, 3, 0, 0,
         {{{0x00, 0xAA, 0x01}, -1, 0, 3}}, SsMouseRival},
        // Corsair (0x1B1C): VOID / VOID PRO / HS70 / HS80 family
        {L"Corsair Headset", DevType::Headset, 0x1B1C,
         {0x1b27, 0x0a14, 0x0a16, 0x0a17, 0x0a1d, 0x0a1a, 0x1b2a, 0x1b23, 0x1b29, 0x0a55, 0x0a51, 0x0a52, 0x0a38, 0x0a4f,
          0x0a2b, 0x0a75, 0x0a56},
         -1, 0xFFC5, 0, {{{0xC9, 0x64}, -1, 0, 5}}, CorsairVoid},
        // HyperX / HP (0x03F0)
        {L"HyperX Cloud II Wireless", DevType::Headset, 0x03F0, {0x0696, 0x018b}, 0, 0xFF90, 0x0303,
         {{Pad({0x06, 0xff, 0xbb, 0x02}, 52), 3, 0x02, 8}, {Pad({0x06, 0xff, 0xbb, 0x03}, 52), 3, 0x03, 5}}, HyperXCloud2},
        {L"HyperX Cloud Alpha Wireless", DevType::Headset, 0x03F0, {0x098d}, -1, 0, 0,
         {{Pad({0x21, 0xbb, 0x03}, 31), 2, 0x03, 4}, {Pad({0x21, 0xbb, 0x0c}, 31), 2, 0x0c, 4},
          {Pad({0x21, 0xbb, 0x0b}, 31), 2, 0x0b, 4}},
         HyperXAlpha},
    };
    return v;
}

bool Matches(const HsSpec& s, const HidIface& f) {
    if (f.vid != s.vid) return false;
    if (std::find(s.pids.begin(), s.pids.end(), f.pid) == s.pids.end()) return false;
    if (s.iface >= 0 && InterfaceNumber(f.path) >= 0 && InterfaceNumber(f.path) != s.iface) return false;
    if (s.page && f.usagePage != s.page) return false;
    if (s.usage && f.usage != s.usage) return false;
    if (!s.page && s.iface < 0 && f.usagePage < 0xFF00) return false;   // vendor collections only
    if (f.usagePage == 0x01 && (f.usage == 0x02 || f.usage == 0x06)) return false;  // never the mouse/keyboard itself
    return f.inLen > 0;
}

bool Exchange(HidPort& p, const Step& st, Bytes& resp) {
    if (!p.Write(st.req)) return false;
    DWORD start = GetTickCount(), limit = (DWORD)st.timeoutMs;
    while (GetTickCount() - start < limit) {
        int n = p.Read(resp, limit - (GetTickCount() - start));
        if (n <= 0) return false;
        if (n < st.minLen) continue;
        if (st.acceptAt >= 0 && (st.acceptAt >= n || resp[st.acceptAt] != st.acceptVal)) continue;   // unrelated report
        return true;
    }
    return false;
}

std::map<std::string, std::wstring> g_goodPath;   // device id -> collection that answered last time
std::map<std::string, Device> g_last;              // last good reading (kept while the headset is off)
std::map<std::string, DWORD> g_lastAsk;            // for devices with minIntervalSec

} // namespace

void PollHeadsets(std::vector<Device>& out) {
    auto ifaces = EnumHid();
    std::map<std::string, bool> done;
    for (const HsSpec& s : Specs()) {
        // collect candidate collections, the one that worked last time first
        std::vector<const HidIface*> cands;
        for (auto& f : ifaces) if (Matches(s, f)) cands.push_back(&f);
        if (cands.empty()) continue;
        std::string id = "hs:" + HexId(cands[0]->vid) + ":" + HexId(cands[0]->pid);
        if (done[id]) continue;
        if (s.minIntervalSec > 0 && g_lastAsk.count(id)) {
            DWORD wait = g_last.count(id) ? (DWORD)s.minIntervalSec * 1000 : 15000;   // retry sooner until the first answer
            if (GetTickCount() - g_lastAsk[id] < wait) {
                if (g_last.count(id)) out.push_back(g_last[id]);   // recent value is still good
                done[id] = true;
                continue;
            }
        }
        g_lastAsk[id] = GetTickCount();
        auto gp = g_goodPath.find(id);
        if (gp != g_goodPath.end())
            std::stable_partition(cands.begin(), cands.end(), [&](const HidIface* f) { return f->path == gp->second; });

        bool answered = false;
        for (const HidIface* f : cands) {
            HidPort port;
            if (!port.Open(*f)) continue;
            std::vector<Bytes> resp;
            bool ok = true;
            for (auto& st : s.steps) {
                Bytes r;
                if (!Exchange(port, st, r)) { ok = false; break; }
                r.resize(64, 0);   // parsers may look a few bytes past a short answer
                resp.push_back(r);
            }
            if (!ok) continue;
            DbgLog("headset %04X:%04X  %s", f->vid, f->pid, HexDump(resp[0].data(), 16).c_str());
            Result x = s.parse(f->pid, resp);
            g_goodPath[id] = f->path;
            answered = true;
            Device d;
            auto last = g_last.find(id);
            if (last != g_last.end()) d = last->second;
            d.id = id;
            d.name = f->product.empty() ? s.name : f->product;
            d.type = s.type;
            d.source = s.source;
            d.online = true;
            d.idle = !x.present;
            if (x.present) {
                d.battery = x.battery;
                d.charging = x.charging;
                g_last[id] = d;
            } else {
                d.charging = false;
            }
            out.push_back(d);
            break;
        }
        if (!answered && g_last.count(id)) {   // receiver present but no answer: show as asleep
            Device d = g_last[id];
            d.idle = true;
            d.charging = false;
            out.push_back(d);
        }
        done[id] = true;
    }
}
