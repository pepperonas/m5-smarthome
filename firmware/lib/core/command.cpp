#include "command.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace core {
namespace {

bool isDigits(const char* s) {
    if (!s || !*s) return false;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

void setStr(char* dst, int cap, const char* src) {
    int n = 0;
    while (src && src[n] && n < cap - 1) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
}

int editDistance(const char* a, const char* b) {
    const int la = static_cast<int>(strlen(a));
    const int lb = static_cast<int>(strlen(b));
    if (la == 0) return lb;
    if (lb == 0) return la;
    if (la > 24 || lb > 24) return 99;      // bounded: no allocation on an MCU
    int prev[25], cur[25];
    for (int j = 0; j <= lb; ++j) prev[j] = j;
    for (int i = 1; i <= la; ++i) {
        cur[0] = i;
        for (int j = 1; j <= lb; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int m = prev[j] + 1;
            if (cur[j - 1] + 1 < m) m = cur[j - 1] + 1;
            if (prev[j - 1] + cost < m) m = prev[j - 1] + cost;
            cur[j] = m;
        }
        memcpy(prev, cur, sizeof(int) * (lb + 1));
    }
    return prev[lb];
}

// Score at or above which a match is acted on, and the lower bar at which we
// merely offer a suggestion. See matchScore() for the scale.
constexpr int kActThreshold = 60;
constexpr int kHintThreshold = 50;

struct Verb {
    const char* word;
    const char* action;
    int arg;
};

// German first — this house is operated in German — with the obvious English
// synonyms alongside, because typing "off" should not fail on principle.
const Verb kOnWords[] = {
    {"an", "on", 0}, {"ein", "on", 0}, {"on", "on", 0}, {"start", "on", 0},
};
const Verb kOffWords[] = {
    {"aus", "off", 0}, {"off", "off", 0}, {"stop", "off", 0}, {"stopp", "off", 0},
};

bool inList(const Verb* list, int n, const char* w, const char** action) {
    for (int i = 0; i < n; ++i) {
        if (strcmp(list[i].word, w) == 0) { *action = list[i].action; return true; }
    }
    return false;
}

}  // namespace

void foldWord(const char* in, char* out, int outLen) {
    int o = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in);
         *p && o < outLen - 2; ++p) {
        unsigned char c = *p;
        // UTF-8 umlauts arrive as two bytes (0xC3 0xA4 etc). Fold them to the
        // ASCII digraph so "Küche", "Kueche" and "kuche" all match.
        if (c == 0xC3 && p[1]) {
            unsigned char n = p[1];
            const char* rep = nullptr;
            if (n == 0xA4 || n == 0x84) rep = "ae";
            else if (n == 0xB6 || n == 0x96) rep = "oe";
            else if (n == 0xBC || n == 0x9C) rep = "ue";
            else if (n == 0x9F) rep = "ss";
            if (rep) {
                out[o++] = rep[0];
                out[o++] = rep[1];
                ++p;
                continue;
            }
            ++p;                      // unknown two-byte sequence: drop it
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[o++] = static_cast<char>(c);
        }
    }
    out[o] = 0;
}

int matchScore(const char* needle, const char* hay) {
    char n[32], h[32];
    foldWord(needle, n, sizeof(n));
    foldWord(hay, h, sizeof(h));
    if (!n[0] || !h[0]) return 0;
    if (strcmp(n, h) == 0) return 100;
    const size_t ln = strlen(n);
    if (strncmp(n, h, ln) == 0) return 90;         // "wohn" -> "wohnzimmer"
    if (strstr(h, n) != nullptr) return 70;        // "zimmer" -> "wohnzimmer"
    // A typo is only forgiven when the words are close in length; otherwise
    // "an" would be a near miss for every three-letter room in the house.
    const size_t lh = strlen(h);
    if (ln >= 4 && lh >= 4) {
        const int dist = editDistance(n, h);
        // One slip lands above kActThreshold and is acted on; two slips stay
        // below it and only produce a suggestion. Guessing which room somebody
        // meant after two typos is how you switch the wrong floor.
        if (dist == 1) return 65;
        if (dist == 2) return 55;
    }
    return 0;
}

namespace {

// Find the best-matching room. Returns id, or 0 and leaves `best` at 0.
int bestRoom(const char* word, const Dash& d, int* best) {
    int bestId = 0, bestScore = 0;
    for (int i = 0; i < d.hue.count; ++i) {
        const int s = matchScore(word, d.hue.rooms[i].name);
        if (s > bestScore) { bestScore = s; bestId = d.hue.rooms[i].id; }
    }
    *best = bestScore;
    return bestId;
}

struct Alias {
    const char* word;
    const char* target;
    const char* action;
    bool confirm;
    const char* label;
};

// One-word shortcuts. Order matters only for readability; matching is exact
// after folding, then fuzzy as a fallback.
const Alias kAliases[] = {
    {"disco",    "disco", "toggle", false, "Disco"},
    {"nebel",    "fog",   "on",     true,  "Nebel AN"},
    {"fog",      "fog",   "on",     true,  "Nebel AN"},
    {"strip",    "lw",    "toggle", false, "Lichtwerk"},
    {"lichtwerk","lw",    "toggle", false, "Lichtwerk"},
    {"licht",    "lw",    "toggle", false, "Lichtwerk"},
    {"yamaha",   "yam",   "toggle", false, "Yamaha"},
    {"receiver", "yam",   "toggle", false, "Yamaha"},
    {"teufel",   "tf",    "power",  false, "Teufel"},
    {"nacht",    "macro", "goodnight", false, "Gute Nacht"},
    {"gutenacht","macro", "goodnight", false, "Gute Nacht"},
    {"wach",     "macro", "wake",   false, "Aufwachen"},
    {"lauter",   "yam",   "vol",    false, "Yamaha lauter"},
    {"leiser",   "yam",   "vol",    false, "Yamaha leiser"},
};

}  // namespace

Intent parseCommand(const char* input, const Dash& d) {
    Intent it;
    if (!input) { setStr(it.hint, sizeof(it.hint), "leer"); return it; }

    // Split into at most four folded words.
    char words[4][24];
    int nWords = 0;
    {
        char cur[24];
        int c = 0;
        for (const char* p = input;; ++p) {
            if (*p && *p != ' ' && c < 23) { cur[c++] = *p; continue; }
            if (c > 0) {
                cur[c] = 0;
                if (nWords < 4) foldWord(cur, words[nWords++], 24);
                c = 0;
            }
            if (!*p) break;
        }
    }
    if (nWords == 0) { setStr(it.hint, sizeof(it.hint), "leer"); return it; }

    // "alles aus" / "aus" on its own means the whole house.
    const char* verbAction = nullptr;
    const bool lastIsOn  = inList(kOnWords, 4, words[nWords - 1], &verbAction);
    const bool lastIsOff = !lastIsOn &&
                           inList(kOffWords, 4, words[nWords - 1], &verbAction);

    if ((nWords == 1 && (lastIsOn || lastIsOff)) ||
        (nWords == 2 && strcmp(words[0], "alles") == 0 && (lastIsOn || lastIsOff))) {
        it.valid = true;
        setStr(it.target, sizeof(it.target), "macro");
        setStr(it.action, sizeof(it.action), lastIsOn ? "wake" : "alloff");
        setStr(it.label, sizeof(it.label), lastIsOn ? "Alles an" : "Alles aus");
        return it;
    }

    // Single-word aliases.
    if (nWords == 1) {
        for (const Alias& a : kAliases) {
            if (strcmp(words[0], a.word) == 0) {
                it.valid = true;
                setStr(it.target, sizeof(it.target), a.target);
                setStr(it.action, sizeof(it.action), a.action);
                setStr(it.label, sizeof(it.label), a.label);
                it.needsConfirm = a.confirm;
                if (strcmp(a.word, "lauter") == 0) { it.arg = 2; it.hasArg = true; }
                if (strcmp(a.word, "leiser") == 0) { it.arg = -2; it.hasArg = true; }
                return it;
            }
        }
    }

    // "<room> an|aus" and "<room> <0..100>"
    if (nWords >= 2) {
        int score = 0;
        const int id = bestRoom(words[0], d, &score);
        if (id && score >= kActThreshold) {
            const Room* r = findRoom(d, id);
            if (lastIsOn || lastIsOff) {
                it.valid = true;
                setStr(it.target, sizeof(it.target), "hue");
                setStr(it.action, sizeof(it.action), lastIsOn ? "on" : "off");
                it.arg = id;
                it.hasArg = true;
                snprintf(it.label, sizeof(it.label), "%s %s",
                         r ? r->name : "Raum", lastIsOn ? "an" : "aus");
                return it;
            }
            if (isDigits(words[nWords - 1])) {
                const int pct = atoi(words[nWords - 1]);
                if (pct >= 0 && pct <= 100) {
                    it.valid = true;
                    setStr(it.target, sizeof(it.target), "hue");
                    setStr(it.action, sizeof(it.action), "bri");
                    it.arg = id;
                    it.hasArg = true;
                    // Hue brightness is 1..254; 0 % would mean "off", which is
                    // a different command, so clamp the bottom to 1.
                    it.arg2 = pct <= 0 ? 1 : (pct * 254) / 100;
                    if (it.arg2 < 1) it.arg2 = 1;
                    it.hasArg2 = true;
                    snprintf(it.label, sizeof(it.label), "%s %d%%",
                             r ? r->name : "Raum", pct);
                    return it;
                }
            }
        }
    }

    // Fuzzy single word against the aliases and rooms, so a near miss says
    // what it thinks you meant instead of just "?".
    int bestScore = 0;
    const char* bestLabel = nullptr;
    for (const Alias& a : kAliases) {
        const int s = matchScore(words[0], a.word);
        if (s > bestScore) { bestScore = s; bestLabel = a.word; }
    }
    for (int i = 0; i < d.hue.count; ++i) {
        const int s = matchScore(words[0], d.hue.rooms[i].name);
        if (s > bestScore) { bestScore = s; bestLabel = d.hue.rooms[i].name; }
    }
    if (bestScore >= kHintThreshold && bestLabel) {
        snprintf(it.hint, sizeof(it.hint), "meintest du: %s", bestLabel);
    } else {
        setStr(it.hint, sizeof(it.hint), "unbekannt");
    }
    return it;
}

const char* completeCommand(const char* input, const Dash& d, char* buf,
                            int bufLen) {
    if (!input || !*input || bufLen < 2) return nullptr;
    char w[24];
    foldWord(input, w, sizeof(w));
    if (!w[0]) return nullptr;

    int bestScore = 0;
    const char* best = nullptr;
    for (const Alias& a : kAliases) {
        const int s = matchScore(w, a.word);
        if (s > bestScore) { bestScore = s; best = a.word; }
    }
    for (int i = 0; i < d.hue.count; ++i) {
        const int s = matchScore(w, d.hue.rooms[i].name);
        if (s > bestScore) { bestScore = s; best = d.hue.rooms[i].name; }
    }
    // Only complete on a prefix hit: silently rewriting a typo into a
    // different room is how you fog the wrong floor.
    if (best && bestScore >= 90) {
        setStr(buf, bufLen, best);
        return buf;
    }
    return nullptr;
}

}  // namespace core
