// Book Reader OTA App — PocketMage
// Drop .md files into /books/ on the SD card.
// On launch: select a book with < / >, press Space to open.
// Reading: < / > to page, FN+< / FN+> to jump chunks, 'b' to return to picker.
// ESC saves position and returns to PocketMage OS.

#include <SD_MMC.h>
#include <globals.h>

#include <Preferences.h>
#include <vector>

// ── Configuration ─────────────────────────────────────────────────────────────
#define SPECIAL_PADDING      20
#define SPACEWIDTH_SYMBOL    "n"
#define WORDWIDTH_BUFFER     0
#define DISPLAY_WIDTH_BUFFER 14
#define HEADING_LINE_PADDING 8
#define NORMAL_LINE_PADDING  4
#define LINES_PER_PAGE       12
#define CONTENT_START_Y      20
#define LINES_PER_CHUNK      100   // source lines per chunk
#define MAX_CHUNKS           256   // stack array cap in buildIndex()

#define MAX_WORD_LEN         63    // max chars per word stored in text pool
#define TEXT_POOL_CAP        10240 // word text bytes for one chunk (~10 KB)
#define WORD_REF_CAP         4000  // total word references for one chunk
#define DISPLAY_LINE_CAP     600   // total display lines for one chunk

// ── App mode ──────────────────────────────────────────────────────────────────
enum AppMode { MODE_PICKER, MODE_READING, MODE_PAGE_JUMP };
static AppMode appMode = MODE_PICKER;

// ── Book picker ───────────────────────────────────────────────────────────────
#define MAX_BOOKS      30
#define MAX_BOOK_NAME  64
#define PICKER_VISIBLE 10

static char s_bookNames[MAX_BOOKS][MAX_BOOK_NAME];
static int  s_bookCount    = 0;
static int  s_pickerSel    = 0;
static int  s_pickerScroll = 0;

// ── Paths ─────────────────────────────────────────────────────────────────────
static const char* const BOOKS_DIR    = "/books";
static const char* const BMARKS_DIR   = "/books/.bmarks";
static const char* const CURRENT_PATH = "/books/.current";

static char s_bookPath       [96];
static char s_bmarkPath      [96];
static char s_idxPath        [96];
static char s_bookDisplayName[MAX_BOOK_NAME];

static void setPaths(const char* fname) {
  snprintf(s_bookPath, sizeof(s_bookPath), "/books/%s", fname);
  // Strip .md from base name for bmark/idx files and display name
  char base[MAX_BOOK_NAME];
  strncpy(base, fname, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  int len = (int)strlen(base);
  if (len > 3 && strcmp(base + len - 3, ".md") == 0)
    base[len - 3] = '\0';
  snprintf(s_bmarkPath, sizeof(s_bmarkPath), "/books/.bmarks/%s.bmark", base);
  snprintf(s_idxPath,   sizeof(s_idxPath),   "/books/.bmarks/%s.idx",   base);
  strncpy(s_bookDisplayName, base, sizeof(s_bookDisplayName) - 1);
  s_bookDisplayName[sizeof(s_bookDisplayName) - 1] = '\0';
}

// Current-book persistence helpers
static bool readCurrentBook(char* out, int outLen) {
  if (!SD_MMC.exists(CURRENT_PATH)) return false;
  File f = SD_MMC.open(CURRENT_PATH, FILE_READ);
  if (!f) return false;
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  if (s.length() == 0) return false;
  strncpy(out, s.c_str(), outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

static void writeCurrentBook(const char* fname) {
  File f = SD_MMC.open(CURRENT_PATH, FILE_WRITE);
  if (f) { f.print(fname); f.close(); }
}

static void clearCurrentBook() {
  SD_MMC.remove(CURRENT_PATH);
}

static void seamlessRestart() {
  Preferences prefs;
  prefs.begin("PocketMage", false);
  prefs.putBool("Seamless_Reboot", true);
  prefs.end();
  ESP.restart();
}

// ── Font setup ────────────────────────────────────────────────────────────────
struct FontMap {
  const GFXfont* normal;
  const GFXfont* normal_B;
  const GFXfont* normal_I;
  const GFXfont* normal_BI;
  const GFXfont* h1;
  const GFXfont* h1_B;
  const GFXfont* h2;
  const GFXfont* h2_B;
  const GFXfont* h3;
  const GFXfont* h3_B;
  const GFXfont* code;
  const GFXfont* quote;
  const GFXfont* list;
};

static FontMap s_fonts;

static void initFonts() {
  s_fonts.normal    = &FreeSerif9pt7b;
  s_fonts.normal_B  = &FreeSerifBold9pt7b;
  s_fonts.normal_I  = &FreeSerif9pt7b;  // italic fallback — no italic variant included
  s_fonts.normal_BI = &FreeSerifBold9pt7b;
  s_fonts.h1        = &FreeSerif12pt7b;
  s_fonts.h1_B      = &FreeSerif12pt7b;
  s_fonts.h2        = &FreeSerifBold9pt7b;
  s_fonts.h2_B      = &FreeSerifBold9pt7b;
  s_fonts.h3        = &FreeSerif9pt7b;
  s_fonts.h3_B      = &FreeSerifBold9pt7b;
  s_fonts.code      = &FreeMonoBold9pt7b;
  s_fonts.quote     = &FreeSerif9pt7b;
  s_fonts.list      = &FreeSerif9pt7b;
}

static const GFXfont* pickFont(char style, bool bold, bool italic) {
  FontMap& fm = s_fonts;
  switch (style) {
    case '1': return bold ? fm.h1_B : fm.h1;
    case '2': return bold ? fm.h2_B : fm.h2;
    case '3': return bold ? fm.h3_B : fm.h3;
    case 'C': return fm.code;
    case '>': return fm.quote;
    case '-': return fm.list;
    case 'L': return fm.list;
    default:
      if (bold && italic) return fm.normal_BI;
      if (bold)           return fm.normal_B;
      if (italic)         return fm.normal_I;
      return fm.normal;
  }
}

// ── Layout pools (all static — zero heap in rendering pipeline) ──────────────
struct WordRef {
  const char* text;
  bool        bold;
  bool        italic;
};

struct DisplayLine {
  ulong    lineIdx;
  uint16_t wordStart;
  uint8_t  wordCount;
};

struct SourceLine {
  char     style;
  uint16_t lineStart;
  uint8_t  lineCount;
  ulong    orderedListNum;
};

static char        s_textPool[TEXT_POOL_CAP];
static int         s_textPoolUsed     = 0;
static WordRef     s_wordRefs[WORD_REF_CAP];
static int         s_wordRefsUsed     = 0;
static DisplayLine s_displayLines[DISPLAY_LINE_CAP];
static int         s_displayLinesUsed = 0;
static SourceLine  s_sourceLines[LINES_PER_CHUNK];
static int         s_sourceLinesUsed  = 0;
static ulong       s_lineIndex        = 0;
static ulong       s_pageStartLine    = 0;

// ── Chunk index ────────────────────────────────────────────────────────────────
struct ChunkInfo {
  size_t offset;
  String heading;
};

static std::vector<ChunkInfo> chunks;
static int   currentChunk     = 0;
static ulong pageIndex        = 0;
static bool  needsRedraw      = false;
static bool  fileError        = false;
static int s_pageCounts[MAX_CHUNKS] = {};  // page count per chunk, BSS
static int s_numPageCounts          = 0;   // how many entries are valid
static int s_totalPages             = 0;   // sum of all s_pageCounts

// ── Page jump ─────────────────────────────────────────────────────────────────
static char s_jumpBuf[5] = "";
static int  s_jumpLen    = 0;

// ── Touch scroll ──────────────────────────────────────────────────────────────
static long int          s_scrollBase          = 0;
static unsigned long     s_scrollCooldownUntil = 0;
static const int         SWIPE_THRESHOLD       = 3;
static const unsigned long SWIPE_COOLDOWN_MS   = 500;

// ── Helpers ───────────────────────────────────────────────────────────────────
static int getMaxPage() {
  return (s_displayLinesUsed <= 0) ? 0 : (s_displayLinesUsed - 1) / LINES_PER_PAGE;
}

// Returns 1-based global page and total, or -1/-1 if page counts are unknown.
// Sums on the fly so currentChunk is always read at call time (after loadBookmark).
static void getGlobalPageInfo(int& outPage, int& outTotal) {
  if (s_totalPages <= 0 || s_numPageCounts == 0) { outPage = -1; outTotal = -1; return; }
  int offset = 0;
  for (int i = 0; i < currentChunk && i < s_numPageCounts; i++)
    offset += s_pageCounts[i];
  outPage  = offset + (int)pageIndex + 1;
  outTotal = s_totalPages;
}

// ── Layout ────────────────────────────────────────────────────────────────────

static const char* internWord(const char* src, int len) {
  int copyLen = (len > MAX_WORD_LEN) ? MAX_WORD_LEN : len;
  if (s_textPoolUsed + copyLen + 1 > TEXT_POOL_CAP) return nullptr;
  char* dst = s_textPool + s_textPoolUsed;
  memcpy(dst, src, copyLen);
  dst[copyLen] = '\0';
  s_textPoolUsed += copyLen + 1;
  return dst;
}

static void commitDisplayLine(int wordStart, int wordCount, SourceLine& src) {
  if (s_displayLinesUsed >= DISPLAY_LINE_CAP) return;
  DisplayLine& dl = s_displayLines[s_displayLinesUsed++];
  dl.lineIdx   = s_lineIndex++;
  dl.wordStart = (uint16_t)wordStart;
  dl.wordCount = (uint8_t)(wordCount > 255 ? 255 : wordCount);
  src.lineCount++;
}

static void layoutSegment(const char* seg, int segLen, bool bold, bool italic,
                          char style, uint16_t textWidth,
                          int& dlWordStart, int& dlWordCount, int& lineWidth,
                          SourceLine& src) {
  display.setFont(pickFont(style, bold, italic));
  int16_t  x1, y1;
  uint16_t sw, sh;
  display.getTextBounds(SPACEWIDTH_SYMBOL, 0, 0, &x1, &y1, &sw, &sh);

  int wStart = 0;
  while (wStart < segLen) {
    int wEnd = wStart;
    while (wEnd < segLen && seg[wEnd] != ' ') wEnd++;
    int wLen = wEnd - wStart;
    if (wLen > 0) {
      const char* wordText = internWord(seg + wStart, wLen);
      if (!wordText || s_wordRefsUsed >= WORD_REF_CAP) return;

      uint16_t wpx, hpx;
      display.getTextBounds(wordText, 0, 0, &x1, &y1, &wpx, &hpx);
      int addWidth = (int)wpx + (int)sw + WORDWIDTH_BUFFER;

      if (lineWidth > 0 && lineWidth + addWidth > (int)textWidth) {
        commitDisplayLine(dlWordStart, dlWordCount, src);
        dlWordStart = s_wordRefsUsed;
        dlWordCount = 0;
        lineWidth   = 0;
      }
      s_wordRefs[s_wordRefsUsed].text   = wordText;
      s_wordRefs[s_wordRefsUsed].bold   = bold;
      s_wordRefs[s_wordRefsUsed].italic = italic;
      s_wordRefsUsed++;
      dlWordCount++;
      lineWidth += addWidth;
    }
    wStart = wEnd + 1;
  }
}

static void layoutSourceLine(const String& text, char style, ulong orderedListNum) {
  if (s_sourceLinesUsed >= LINES_PER_CHUNK) return;

  SourceLine& src    = s_sourceLines[s_sourceLinesUsed++];
  src.style          = style;
  src.orderedListNum = orderedListNum;
  src.lineStart      = (uint16_t)s_displayLinesUsed;
  src.lineCount      = 0;

  if (style == 'B' || style == 'H') {
    commitDisplayLine(s_wordRefsUsed, 0, src);
    return;
  }

  uint16_t textWidth = (uint16_t)(display.width() - DISPLAY_WIDTH_BUFFER);
  if (style == '>' || style == 'C')
    textWidth -= SPECIAL_PADDING;
  else if (style == '-' || style == 'L')
    textWidth -= 2 * SPECIAL_PADDING;

  int dlWordStart = s_wordRefsUsed;
  int dlWordCount = 0;
  int lineWidth   = 0;

  const char* raw = text.c_str();
  int n = (int)text.length();
  int i = 0;
  while (i < n) {
    bool bold = false, italic = false;
    int segStart, segEnd;

    if (raw[i] == '*' && i + 1 < n && raw[i + 1] == '*') {
      bold     = true;
      segStart = i + 2;
      const char* e = strstr(raw + segStart, "**");
      segEnd = e ? (int)(e - raw) : n;
      i      = segEnd + 2;
    } else if (raw[i] == '*') {
      italic   = true;
      segStart = i + 1;
      const char* e = strchr(raw + segStart, '*');
      segEnd = e ? (int)(e - raw) : n;
      i      = segEnd + 1;
    } else {
      const char* nb = strstr(raw + i, "**");
      const char* ni = strchr(raw + i, '*');
      segStart = i;
      segEnd   = n;
      if (nb) segEnd = min(segEnd, (int)(nb - raw));
      if (ni) segEnd = min(segEnd, (int)(ni - raw));
      i = segEnd;
    }

    if (segEnd > segStart)
      layoutSegment(raw + segStart, segEnd - segStart, bold, italic,
                    style, textWidth, dlWordStart, dlWordCount, lineWidth, src);
  }

  if (dlWordCount > 0)
    commitDisplayLine(dlWordStart, dlWordCount, src);
}

// ── Index building ─────────────────────────────────────────────────────────────
static void loadChunk(int idx, bool triggerRedraw);  // forward declaration

static void buildIndex() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(1, 9, "Indexing...");
  u8g2.sendBuffer();

  chunks.clear();
  File f = SD_MMC.open(s_bookPath, FILE_READ);
  if (!f) {
    fileError = true;
    return;
  }

  char headBuf[64] = "";
  int lineCount = 0;

  ChunkInfo first;
  first.offset  = 0;
  first.heading = "";
  chunks.push_back(first);

  while (f.available()) {
    char buf[256];
    int len = 0;
    while (f.available() && len < 255) {
      char c = (char)f.read();
      if (c == '\n') break;
      buf[len++] = c;
    }
    buf[len] = '\0';
    if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

    if (buf[0] == '#' && buf[1] == ' ') {
      size_t hlen = strlen(buf + 2);
      if (hlen >= sizeof(headBuf)) hlen = sizeof(headBuf) - 1;
      memcpy(headBuf, buf + 2, hlen);
      headBuf[hlen] = '\0';
      if (chunks.back().heading.length() == 0)
        chunks.back().heading = String(headBuf);
    }

    lineCount++;
    if (lineCount % LINES_PER_CHUNK == 0) {
      ChunkInfo ci;
      ci.offset  = (size_t)f.position();
      ci.heading = String(headBuf);
      chunks.push_back(ci);
    }
  }
  f.close();

  // Count pages per chunk into a short-lived stack array (512 bytes, BSS-free)
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(1, 9, "Counting pages...");
  u8g2.sendBuffer();
  int nChunks = (int)chunks.size();
  if (nChunks > MAX_CHUNKS) nChunks = MAX_CHUNKS;
  int pageCounts[MAX_CHUNKS] = {};
  for (int i = 0; i < nChunks; i++) {
    loadChunk(i, false);
    pageCounts[i] = getMaxPage() + 1;
  }
  needsRedraw = false;

  // Populate s_pageCounts and persist to .idx
  s_numPageCounts = nChunks;
  s_totalPages    = 0;
  for (int i = 0; i < nChunks; i++) {
    s_pageCounts[i] = pageCounts[i];
    s_totalPages   += pageCounts[i];
  }
  if (!SD_MMC.exists(BMARKS_DIR)) SD_MMC.mkdir(BMARKS_DIR);
  File idx = SD_MMC.open(s_idxPath, FILE_WRITE);
  if (idx) {
    for (int i = 0; i < nChunks; i++) {
      idx.print(String((unsigned long)chunks[i].offset));
      idx.print('\t');
      idx.print(chunks[i].heading);
      idx.print('\t');
      idx.print(pageCounts[i]);
      idx.print('\n');
    }
    idx.close();
  }
}

static bool loadIndex() {
  if (!SD_MMC.exists(s_idxPath)) return false;
  File f = SD_MMC.open(s_idxPath, FILE_READ);
  if (!f) return false;

  chunks.clear();
  s_numPageCounts   = 0;
  s_totalPages      = 0;
  bool allHavePageCount = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int tab = line.indexOf('\t');
    ChunkInfo ci;
    int pageCount = 0;
    if (tab >= 0) {
      ci.offset   = (size_t)line.substring(0, tab).toInt();
      int tab2    = line.indexOf('\t', tab + 1);
      if (tab2 >= 0) {
        ci.heading = line.substring(tab + 1, tab2);
        pageCount  = line.substring(tab2 + 1).toInt();
      } else {
        ci.heading = line.substring(tab + 1);
      }
    } else {
      ci.offset  = (size_t)line.toInt();
      ci.heading = "";
    }
    if (pageCount <= 0) allHavePageCount = false;
    if (s_numPageCounts < MAX_CHUNKS) s_pageCounts[s_numPageCounts] = pageCount;
    s_numPageCounts++;
    s_totalPages += pageCount;
    chunks.push_back(ci);
  }
  f.close();
  // If any chunk is missing page counts, force a rebuild
  if (!allHavePageCount) {
    chunks.clear();
    s_numPageCounts = 0;
    s_totalPages    = 0;
    return false;
  }
  return !chunks.empty();
}

static void buildOrLoadIndex() {
  if (!loadIndex()) buildIndex();
  if (chunks.empty()) {
    ChunkInfo fallback;
    fallback.offset  = 0;
    fallback.heading = "";
    chunks.push_back(fallback);
  }
}

// ── Chunk loading ──────────────────────────────────────────────────────────────
static void loadChunk(int idx, bool triggerRedraw) {
  if (idx < 0 || idx >= (int)chunks.size()) return;

  File f = SD_MMC.open(s_bookPath, FILE_READ);
  if (!f) {
    fileError = true;
    return;
  }

  f.seek(chunks[idx].offset);
  size_t endOffset = (idx + 1 < (int)chunks.size()) ? chunks[idx + 1].offset : 0;

  s_textPoolUsed     = 0;
  s_wordRefsUsed     = 0;
  s_displayLinesUsed = 0;
  s_sourceLinesUsed  = 0;
  s_lineIndex        = 0;

  ulong listCounter = 1;
  int   lineCount   = 0;

  while (f.available()) {
    if (endOffset != 0 && (size_t)f.position() >= endOffset) break;
    if (lineCount >= LINES_PER_CHUNK) break;

    String raw = f.readStringUntil('\n');
    raw.trim();

    char   st = 'T';
    String content;

    if (raw.length() == 0) {
      st = 'B';
    } else if (raw == "---") {
      st = 'H';
    } else if (raw.startsWith("# ")) {
      st = '1'; content = raw.substring(2);
      if (chunks[idx].heading.length() == 0) chunks[idx].heading = content;
    } else if (raw.startsWith("## ")) {
      st = '2'; content = raw.substring(3);
    } else if (raw.startsWith("### ")) {
      st = '3'; content = raw.substring(4);
    } else if (raw.startsWith("> ")) {
      st = '>'; content = raw.substring(2);
    } else if (raw.startsWith("- ")) {
      st = '-'; content = raw.substring(2); listCounter = 1;
    } else if (raw.startsWith("```")) {
      st = 'C';
    } else if (raw.length() >= 3 && isDigit(raw[0]) && raw[1] == '.' && raw[2] == ' ') {
      st = 'L'; content = raw.substring(3);
    } else {
      content = std::move(raw);
    }

    ulong listNum = (st == 'L') ? listCounter++ : 0;
    if (st != 'L') listCounter = 1;

    layoutSourceLine(content, st, listNum);
    lineCount++;
  }
  f.close();

  if (s_sourceLinesUsed == 0)
    layoutSourceLine(String("(empty)"), 'T', 0);

  if (triggerRedraw) needsRedraw = true;
}

// ── Bookmarks ─────────────────────────────────────────────────────────────────
static void loadBookmark() {
  if (!SD_MMC.exists(s_bmarkPath)) return;
  File f = SD_MMC.open(s_bmarkPath, FILE_READ);
  if (!f) return;
  String data = f.readStringUntil('\n');
  f.close();

  int sep = data.indexOf(':');
  if (sep < 0) return;

  int   ck = data.substring(0, sep).toInt();
  ulong pg = (ulong)data.substring(sep + 1).toInt();

  if (ck >= 0 && ck < (int)chunks.size()) {
    currentChunk = ck;
    pageIndex    = pg;
  }
}

static void saveBookmark() {
  if (!SD_MMC.exists(BMARKS_DIR)) SD_MMC.mkdir(BMARKS_DIR);
  File f = SD_MMC.open(s_bmarkPath, FILE_WRITE);
  if (!f) return;
  f.print(String(currentChunk) + ":" + String((unsigned long)pageIndex));
  f.close();
}

// ── Book scanning ─────────────────────────────────────────────────────────────
static void scanBooks() {
  s_bookCount = 0;
  File dir = SD_MMC.open(BOOKS_DIR);
  if (!dir || !dir.isDirectory()) return;

  File entry = dir.openNextFile();
  while (entry && s_bookCount < MAX_BOOKS) {
    if (!entry.isDirectory()) {
      const char* full  = entry.name();
      const char* slash = strrchr(full, '/');
      const char* fname = slash ? slash + 1 : full;
      int flen = (int)strlen(fname);
      if (flen > 3 && strcmp(fname + flen - 3, ".md") == 0) {
        strncpy(s_bookNames[s_bookCount], fname, MAX_BOOK_NAME - 1);
        s_bookNames[s_bookCount][MAX_BOOK_NAME - 1] = '\0';
        s_bookCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
}

// ── OLED update ───────────────────────────────────────────────────────────────
static void updateOLED() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);

  if (appMode == MODE_PICKER) {
    u8g2.drawStr(1, 9, "Book Reader");
    char hint[48];
    snprintf(hint, sizeof(hint), "< > select  SPC open  (%d books)", s_bookCount);
    u8g2.drawStr(1, 20, hint);
  } else if (appMode == MODE_PAGE_JUMP) {
    char prompt[32];
    int maxPg = (s_totalPages > 0) ? s_totalPages : (getMaxPage() + 1);
    snprintf(prompt, sizeof(prompt), "Go to page (1-%d):", maxPg);
    u8g2.drawStr(1, 9, prompt);
    u8g2.drawStr(1, 20, s_jumpBuf);
  } else {
    if (chunks.empty() || currentChunk >= (int)chunks.size()) {
      u8g2.sendBuffer();
      return;
    }
    int globalPage, totalPages;
    getGlobalPageInfo(globalPage, totalPages);

    float progress;
    if (globalPage > 0 && totalPages > 0) {
      progress = (float)(globalPage - 1) / (float)totalPages;
    } else {
      int totalCk = (int)chunks.size();
      int maxPg   = getMaxPage();
      progress = (totalCk <= 1)
          ? (maxPg > 0 ? (float)pageIndex / (float)maxPg : 1.0f)
          : ((float)currentChunk + (maxPg > 0 ? (float)pageIndex / (float)maxPg : 1.0f)) /
                (float)totalCk;
    }
    progress = constrain(progress, 0.0f, 1.0f);
    int barFill = (int)(253.0f * progress);

    String title = chunks[currentChunk].heading;
    if (title.length() == 0) title = String(s_bookDisplayName);
    if ((int)title.length() > 36) title = title.substring(0, 35) + "~";
    u8g2.drawStr(1, 9, title.c_str());

    String info;
    if (globalPage > 0 && totalPages > 0) {
      info = "Pg " + String(globalPage) + "/" + String(totalPages);
    } else {
      info = "Pg " + String((unsigned long)(pageIndex + 1)) + "/" + String(getMaxPage() + 1);
    }
    u8g2.drawStr(1, 20, info.c_str());

    u8g2.drawFrame(0, 25, 256, 7);
    if (barFill > 0) u8g2.drawBox(1, 26, min(barFill, 254), 5);
  }

  u8g2.sendBuffer();
}

// ── Document rendering ────────────────────────────────────────────────────────
static int renderSourceLine(int si, int startX, int startY) {
  const SourceLine& src   = s_sourceLines[si];
  char              style = src.style;

  if (src.lineCount > 0 &&
      s_displayLines[src.lineStart + src.lineCount - 1].lineIdx < s_pageStartLine)
    return 0;

  if (style == 'H') {
    display.drawFastHLine(0, startY + 3, display.width(), GxEPD_BLACK);
    display.drawFastHLine(0, startY + 4, display.width(), GxEPD_BLACK);
    return 8;
  }
  if (style == 'B') return 12;

  int drawX = startX;
  if (style == '>')
    drawX += SPECIAL_PADDING;
  else if (style == '-' || style == 'L')
    drawX += 2 * SPECIAL_PADDING;
  else if (style == 'C')
    drawX += SPECIAL_PADDING / 2;

  int cursorY = startY;

  for (int li = src.lineStart; li < src.lineStart + src.lineCount; li++) {
    const DisplayLine& dl = s_displayLines[li];
    if (dl.lineIdx < s_pageStartLine) continue;

    int      cx      = drawX;
    uint16_t max_hpx = 0;

    for (int wi = dl.wordStart; wi < dl.wordStart + dl.wordCount; wi++) {
      const WordRef& w = s_wordRefs[wi];
      display.setFont(pickFont(style, w.bold, w.italic));
      int16_t  x1, y1;
      uint16_t wpx, hpx;
      display.getTextBounds(w.text, cx, cursorY, &x1, &y1, &wpx, &hpx);
      if (hpx > max_hpx) max_hpx = hpx;
    }
    if (style == '1' || style == '2' || style == '3') max_hpx += 4;

    for (int wi = dl.wordStart; wi < dl.wordStart + dl.wordCount; wi++) {
      const WordRef& w = s_wordRefs[wi];
      display.setFont(pickFont(style, w.bold, w.italic));
      int16_t  x1, y1;
      uint16_t wpx, hpx, sw, sh;
      display.getTextBounds(w.text, cx, cursorY, &x1, &y1, &wpx, &hpx);
      display.getTextBounds(SPACEWIDTH_SYMBOL, cx, cursorY, &x1, &y1, &sw, &sh);
      display.setCursor(cx, cursorY + max_hpx);
      display.print(w.text);
      cx += (int)wpx + (int)sw;
    }

    uint8_t pad = (style == '1' || style == '2' || style == '3') ? HEADING_LINE_PADDING
                                                                  : NORMAL_LINE_PADDING;
    cursorY += (int)max_hpx + (int)pad;
  }

  if (style == '>') {
    display.drawFastVLine(SPECIAL_PADDING / 2, startY, cursorY - startY, GxEPD_BLACK);
    display.drawFastVLine(SPECIAL_PADDING / 2 + 1, startY, cursorY - startY, GxEPD_BLACK);
  } else if (style == 'C') {
    display.drawFastVLine(SPECIAL_PADDING / 4, startY, cursorY - startY, GxEPD_BLACK);
    display.drawFastVLine(SPECIAL_PADDING / 4 + 1, startY, cursorY - startY, GxEPD_BLACK);
    display.drawFastVLine(display.width() - SPECIAL_PADDING / 4, startY, cursorY - startY,
                          GxEPD_BLACK);
    display.drawFastVLine(display.width() - SPECIAL_PADDING / 4 - 1, startY, cursorY - startY,
                          GxEPD_BLACK);
  } else if (style == '1' || style == '2' || style == '3') {
    display.drawFastHLine(0, cursorY - 2, display.width(), GxEPD_BLACK);
    display.drawFastHLine(0, cursorY - 3, display.width(), GxEPD_BLACK);
  } else if (style == '-') {
    display.fillCircle(drawX - 8, startY + 8, 3, GxEPD_BLACK);
  } else if (style == 'L') {
    char num[16];
    snprintf(num, sizeof(num), "%lu. ", src.orderedListNum);
    display.setFont(pickFont('T', false, false));
    int16_t  x1, y1;
    uint16_t wpx, hpx;
    display.getTextBounds(num, 0, 0, &x1, &y1, &wpx, &hpx);
    display.setCursor(drawX - (int)wpx - 5, startY + (int)hpx);
    display.print(num);
  }

  return cursorY - startY;
}

static void renderDocument(int startX, int startY) {
  s_pageStartLine = pageIndex * LINES_PER_PAGE;
  int cursorY = startY;
  for (int si = 0; si < s_sourceLinesUsed; si++) {
    if (cursorY >= display.height() - 6) break;
    cursorY += renderSourceLine(si, startX, cursorY);
  }
}

// ── Entry points ──────────────────────────────────────────────────────────────
void APP_INIT() {
  initFonts();
  fileError    = false;
  currentChunk = 0;
  pageIndex    = 0;
  needsRedraw  = true;

  // Check if a book was previously selected
  char fname[MAX_BOOK_NAME] = "";
  if (readCurrentBook(fname, sizeof(fname))) {
    char checkPath[96];
    snprintf(checkPath, sizeof(checkPath), "/books/%s", fname);
    if (SD_MMC.exists(checkPath)) {
      setPaths(fname);
      appMode = MODE_READING;
      buildOrLoadIndex();
      if (!fileError) {
        loadBookmark();
        loadChunk(currentChunk, true);
        int mp = getMaxPage();
        if ((int)pageIndex > mp) pageIndex = (ulong)mp;
      }
      return;
    }
    // File no longer exists — fall through to picker
    clearCurrentBook();
  }

  // Picker mode: scan for .md files
  appMode = MODE_PICKER;
  scanBooks();
  s_pickerSel    = 0;
  s_pickerScroll = 0;

  // Auto-select if only one book
  if (s_bookCount == 1) {
    writeCurrentBook(s_bookNames[0]);
    seamlessRestart();
  }

  updateOLED();
}

void processKB_APP() {
  // ── Touch scroll (reading mode only) ──────────────────────────────────────
  if (appMode == MODE_READING) {
    TOUCH().updateScrollRaw();
    long int cur   = TOUCH().getDynamicScroll();
    long int delta = cur - s_scrollBase;

    if (millis() >= s_scrollCooldownUntil) {
      if (delta >= SWIPE_THRESHOLD) {
        s_scrollBase          = cur;
        s_scrollCooldownUntil = millis() + SWIPE_COOLDOWN_MS;
        if ((int)pageIndex < getMaxPage()) {
          pageIndex++;
          needsRedraw = true;
        } else if (currentChunk + 1 < (int)chunks.size()) {
          currentChunk++;
          pageIndex = 0;
          saveBookmark();
          seamlessRestart();
        }
      } else if (delta <= -SWIPE_THRESHOLD) {
        s_scrollBase          = cur;
        s_scrollCooldownUntil = millis() + SWIPE_COOLDOWN_MS;
        if (pageIndex > 0) {
          pageIndex--;
          needsRedraw = true;
        } else if (currentChunk > 0) {
          currentChunk--;
          pageIndex = 65535;
          saveBookmark();
          seamlessRestart();
        }
      }
    } else {
      s_scrollBase = cur;  // drain accumulation during cooldown
    }
  }

  char ch = KB().updateKeypress();
  if (!ch) return;

  if (appMode == MODE_PICKER) {
    if (ch == 27 || ch == 65) {  // ESC or A — exit to OS
      rebootToPocketMage();
      return;
    }
    if (ch == 21) {  // RIGHT — next book in list
      if (s_pickerSel < s_bookCount - 1) {
        s_pickerSel++;
        if (s_pickerSel >= s_pickerScroll + PICKER_VISIBLE)
          s_pickerScroll = s_pickerSel - PICKER_VISIBLE + 1;
        needsRedraw = true;
      }
    } else if (ch == 19) {  // LEFT — prev book in list
      if (s_pickerSel > 0) {
        s_pickerSel--;
        if (s_pickerSel < s_pickerScroll)
          s_pickerScroll = s_pickerSel;
        needsRedraw = true;
      }
    } else if (ch == 32 || ch == 13) {  // Space or Enter — open selected book
      if (s_bookCount > 0) {
        writeCurrentBook(s_bookNames[s_pickerSel]);
        seamlessRestart();
      }
    }
    return;
  }

  // ── Page jump mode ──────────────────────────────────────────────────────────
  if (appMode == MODE_PAGE_JUMP) {
    if (ch == 18) {  // FN — needed to reach number layer
      KB().setKeyboardState(KB().getKeyboardState() == FUNC ? NORMAL : FUNC);
      return;
    }
    if (ch >= '0' && ch <= '9') {
      if (s_jumpLen < 4) {
        s_jumpBuf[s_jumpLen++] = ch;
        s_jumpBuf[s_jumpLen]   = '\0';
        updateOLED();
      }
    } else if (ch == 13 || ch == 32) {  // Enter or Space — commit
      if (s_jumpLen > 0) {
        int target = atoi(s_jumpBuf);
        if (s_totalPages > 0 && s_numPageCounts > 0) {
          // Global page navigation across chunks
          if (target < 1)           target = 1;
          if (target > s_totalPages) target = s_totalPages;
          int offset      = 0;
          int targetChunk = s_numPageCounts - 1;
          int localPage   = s_pageCounts[targetChunk] - 1;
          for (int i = 0; i < s_numPageCounts; i++) {
            if (offset + s_pageCounts[i] >= target) {
              targetChunk = i;
              localPage   = target - offset - 1;
              break;
            }
            offset += s_pageCounts[i];
          }
          if (targetChunk != currentChunk) {
            currentChunk = targetChunk;
            pageIndex    = (ulong)localPage;
            appMode      = MODE_READING;
            saveBookmark();
            seamlessRestart();
            return;
          }
          pageIndex   = (ulong)localPage;
          needsRedraw = true;
        } else {
          // Fallback: local chunk pages only
          int maxPg = getMaxPage();
          if (target < 1)         target = 1;
          if (target > maxPg + 1) target = maxPg + 1;
          pageIndex   = (ulong)(target - 1);
          needsRedraw = true;
        }
      }
      KB().setKeyboardState(NORMAL);
      appMode = MODE_READING;
      updateOLED();
    } else if (ch == 27 || ch == 8) {  // ESC or Backspace — cancel
      KB().setKeyboardState(NORMAL);
      appMode = MODE_READING;
      updateOLED();
    }
    return;
  }

  // ── Reading mode ────────────────────────────────────────────────────────────
  if (ch == 27 || ch == 65) {  // ESC or A — save & return to OS (keeps .current)
    saveBookmark();
    rebootToPocketMage();
    return;
  }

  if (ch == 'b' || ch == 'B') {  // bookmark and return to picker
    saveBookmark();
    clearCurrentBook();
    seamlessRestart();
    return;
  }

  if (ch == 'g' || ch == 'G') {  // jump to page
    s_jumpLen    = 0;
    s_jumpBuf[0] = '\0';
    appMode      = MODE_PAGE_JUMP;
    updateOLED();
    return;
  }

  if (ch == 17) {  // SHIFT
    KB().setKeyboardState(KB().getKeyboardState() == SHIFT ? NORMAL : SHIFT);
    return;
  }
  if (ch == 18) {  // FN
    KB().setKeyboardState(KB().getKeyboardState() == FUNC ? NORMAL : FUNC);
    return;
  }

  if (ch == 21) {  // RIGHT — next page
    if ((int)pageIndex < getMaxPage()) {
      pageIndex++;
      needsRedraw = true;
    } else if (currentChunk + 1 < (int)chunks.size()) {
      currentChunk++;
      pageIndex = 0;
      saveBookmark();
      seamlessRestart();
    }

  } else if (ch == 19) {  // LEFT — prev page
    if (pageIndex > 0) {
      pageIndex--;
      needsRedraw = true;
    } else if (currentChunk > 0) {
      currentChunk--;
      pageIndex = 65535;  // sentinel: APP_INIT clamps to getMaxPage()
      saveBookmark();
      seamlessRestart();
    }

  } else if (ch == 6) {  // RIGHT (FN) — next chunk
    if (currentChunk + 1 < (int)chunks.size()) {
      currentChunk++;
      pageIndex = 0;
      saveBookmark();
      seamlessRestart();
    }
    KB().setKeyboardState(NORMAL);

  } else if (ch == 12) {  // LEFT (FN) — prev chunk
    if (currentChunk > 0) {
      currentChunk--;
      pageIndex = 65535;
      saveBookmark();
      seamlessRestart();
    }
    KB().setKeyboardState(NORMAL);

  } else if (KB().getKeyboardState() == FUNC || KB().getKeyboardState() == FN_SHIFT) {
    KB().setKeyboardState(NORMAL);
  }
}

void einkHandler_APP() {
  if (!needsRedraw) return;
  needsRedraw = false;

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  if (appMode == MODE_PICKER) {
    // Header
    display.setFont(&Font5x7Fixed);
    display.setCursor(4, 11);
    display.print("Books");
    display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

    if (s_bookCount == 0) {
      display.setFont(&FreeSerif9pt7b);
      display.setCursor(10, 50);
      display.print("No .md files found in /books/");
    } else {
      display.setFont(&FreeSerif9pt7b);
      int lineH = 20;
      int y     = 14 + lineH;  // first item baseline
      for (int i = s_pickerScroll;
           i < s_bookCount && i < s_pickerScroll + PICKER_VISIBLE;
           i++) {
        if (i == s_pickerSel) {
          display.fillRect(0, y - lineH + 2, display.width(), lineH, GxEPD_BLACK);
          display.setTextColor(GxEPD_WHITE);
        } else {
          display.setTextColor(GxEPD_BLACK);
        }
        // Show name without .md extension
        char displayName[MAX_BOOK_NAME];
        strncpy(displayName, s_bookNames[i], sizeof(displayName) - 1);
        displayName[sizeof(displayName) - 1] = '\0';
        int dlen = (int)strlen(displayName);
        if (dlen > 3 && strcmp(displayName + dlen - 3, ".md") == 0)
          displayName[dlen - 3] = '\0';
        display.setCursor(6, y);
        display.print(displayName);
        y += lineH;
      }
      display.setTextColor(GxEPD_BLACK);
    }

    // Footer hint
    display.setFont(&Font5x7Fixed);
    display.setCursor(2, display.height() - 2);
    display.print("< > navigate   SPC open   ESC exit");

    EINK().refresh();
    updateOLED();
    return;
  }

  // ── Reading mode render ──────────────────────────────────────────────────────
  if (fileError || chunks.empty() || currentChunk >= (int)chunks.size()) {
    display.setFont(&FreeSerif9pt7b);
    display.setCursor(10, 30);
    display.print(fileError ? "Cannot open book file" : "No content found");
    EINK().refresh();
    return;
  }

  display.setFont(&Font5x7Fixed);
  String header = chunks[currentChunk].heading;
  if (header.length() == 0) header = String(s_bookDisplayName);
  if ((int)header.length() > 44) header = header.substring(0, 43) + "~";
  display.setCursor(4, 11);
  display.print(header);
  display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

  renderDocument(4, CONTENT_START_Y);

  EINK().refresh();
  updateOLED();
}
