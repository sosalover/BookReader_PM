// Book Reader OTA App — PocketMage
// Place your .md file at /books/book.md on the SD card.
// Navigation: < / > to page through, FN+< / FN+> to jump chunks.
// Press ESC to save your position and return to PocketMage OS.

#include <SD_MMC.h>
#include <globals.h>

#include <vector>

// ── Configuration ─────────────────────────────────────────────────────────────
static const char* const BOOK_PATH  = "/books/book.md";
static const char* const BMARK_DIR  = "/books/.bmarks";
static const char* const BMARK_PATH = "/books/.bmarks/book.bmark";
static const char* const IDX_PATH   = "/books/.bmarks/book.idx";

#define SPECIAL_PADDING      20
#define SPACEWIDTH_SYMBOL    "n"
#define WORDWIDTH_BUFFER     0
#define DISPLAY_WIDTH_BUFFER 14
#define HEADING_LINE_PADDING 8
#define NORMAL_LINE_PADDING  4
#define LINES_PER_PAGE       12
#define CONTENT_START_Y      20
#define LINES_PER_CHUNK      100   // source lines per chunk

#define MAX_WORD_LEN         63    // max chars per word stored in text pool
#define TEXT_POOL_CAP        10240 // word text bytes for one chunk (~10 KB)
#define WORD_REF_CAP         4000  // total word references for one chunk
#define DISPLAY_LINE_CAP     600   // total display lines for one chunk

// ── Font setup ────────────────────────────────────────────────────────────────
enum FontFamily { serif = 0, sans = 1, mono = 2 };
static uint8_t fontStyle = serif;

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

static FontMap fonts[3];

static void initFonts() {
  fonts[serif].normal    = &FreeSerif9pt7b;
  fonts[serif].normal_B  = &FreeSerifBold9pt7b;
  fonts[serif].normal_I  = &FreeSerif9pt7b;  // italic fallback
  fonts[serif].normal_BI = &FreeSerifBold9pt7b;
  fonts[serif].h1        = &FreeSerif12pt7b;
  fonts[serif].h1_B      = &FreeSerif12pt7b;
  fonts[serif].h2        = &FreeSerifBold9pt7b;
  fonts[serif].h2_B      = &FreeSerifBold9pt7b;
  fonts[serif].h3        = &FreeSerif9pt7b;
  fonts[serif].h3_B      = &FreeSerifBold9pt7b;
  fonts[serif].code      = &FreeMonoBold9pt7b;
  fonts[serif].quote     = &FreeSerif9pt7b;
  fonts[serif].list      = &FreeSerif9pt7b;
}

static const GFXfont* pickFont(char style, bool bold, bool italic) {
  FontMap& fm = fonts[fontStyle];
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
  ulong    scrollIdx;
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
static ulong       s_scrollCounter    = 0;
static ulong       lineScroll         = 0;

// ── Chunk index ────────────────────────────────────────────────────────────────
struct ChunkInfo {
  size_t offset;
  String heading;
};

static std::vector<ChunkInfo> chunks;
static int   currentChunk = 0;
static ulong pageIndex    = 0;
static bool  needsRedraw  = false;
static bool  fileError    = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
static int getMaxPage() {
  return (s_displayLinesUsed <= 0) ? 0 : (s_displayLinesUsed - 1) / LINES_PER_PAGE;
}

// ── Layout ────────────────────────────────────────────────────────────────────

// Copy a word into the persistent text pool. Returns pointer or nullptr if full.
static const char* internWord(const char* src, int len) {
  int copyLen = (len > MAX_WORD_LEN) ? MAX_WORD_LEN : len;
  if (s_textPoolUsed + copyLen + 1 > TEXT_POOL_CAP) return nullptr;
  char* dst = s_textPool + s_textPoolUsed;
  memcpy(dst, src, copyLen);
  dst[copyLen] = '\0';
  s_textPoolUsed += copyLen + 1;
  return dst;
}

// Commit the current in-progress display line to the pool.
static void commitDisplayLine(int wordStart, int wordCount, SourceLine& src) {
  if (s_displayLinesUsed >= DISPLAY_LINE_CAP) return;
  DisplayLine& dl = s_displayLines[s_displayLinesUsed++];
  dl.scrollIdx = s_scrollCounter++;
  dl.wordStart = (uint16_t)wordStart;
  dl.wordCount = (uint8_t)(wordCount > 255 ? 255 : wordCount);
  src.lineCount++;
}

// Layout one uniformly-styled text segment into display lines.
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

// Parse markdown inline formatting and layout a source line into display lines.
static void layoutSourceLine(const String& text, char style, ulong orderedListNum) {
  if (s_sourceLinesUsed >= LINES_PER_CHUNK) return;

  SourceLine& src    = s_sourceLines[s_sourceLinesUsed++];
  src.style          = style;
  src.orderedListNum = orderedListNum;
  src.lineStart      = (uint16_t)s_displayLinesUsed;
  src.lineCount      = 0;

  // Blank lines and rules get a scroll slot but no words.
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
static void buildIndex() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(1, 9, "Indexing...");
  u8g2.sendBuffer();

  chunks.clear();
  File f = SD_MMC.open(BOOK_PATH, FILE_READ);
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

  if (!SD_MMC.exists(BMARK_DIR)) SD_MMC.mkdir(BMARK_DIR);
  File idx = SD_MMC.open(IDX_PATH, FILE_WRITE);
  if (idx) {
    for (auto& ci : chunks) {
      idx.print(String((unsigned long)ci.offset));
      idx.print('\t');
      idx.print(ci.heading);
      idx.print('\n');
    }
    idx.close();
  }
}

static bool loadIndex() {
  if (!SD_MMC.exists(IDX_PATH)) return false;
  File f = SD_MMC.open(IDX_PATH, FILE_READ);
  if (!f) return false;

  chunks.clear();
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int tab = line.indexOf('\t');
    ChunkInfo ci;
    if (tab >= 0) {
      ci.offset  = (size_t)line.substring(0, tab).toInt();
      ci.heading = line.substring(tab + 1);
    } else {
      ci.offset  = (size_t)line.toInt();
      ci.heading = "";
    }
    chunks.push_back(ci);
  }
  f.close();
  return !chunks.empty();
}

static void buildOrLoadIndex() {
  if (!loadIndex()) buildIndex();
  if (chunks.empty()) {
    ChunkInfo fallback;
    fallback.offset  = 0;
    fallback.heading = "Book";
    chunks.push_back(fallback);
  }
}

// ── Chunk loading ──────────────────────────────────────────────────────────────
static void loadChunk(int idx) {
  if (idx < 0 || idx >= (int)chunks.size()) return;

  File f = SD_MMC.open(BOOK_PATH, FILE_READ);
  if (!f) {
    fileError = true;
    return;
  }

  f.seek(chunks[idx].offset);
  size_t endOffset = (idx + 1 < (int)chunks.size()) ? chunks[idx + 1].offset : 0;

  // Reset all layout pools for this chunk
  s_textPoolUsed     = 0;
  s_wordRefsUsed     = 0;
  s_displayLinesUsed = 0;
  s_sourceLinesUsed  = 0;
  s_scrollCounter    = 0;

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

  needsRedraw = true;
}

// ── Bookmarks ─────────────────────────────────────────────────────────────────
static void loadBookmark() {
  if (!SD_MMC.exists(BMARK_PATH)) return;
  File f = SD_MMC.open(BMARK_PATH, FILE_READ);
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
  if (!SD_MMC.exists(BMARK_DIR)) SD_MMC.mkdir(BMARK_DIR);
  File f = SD_MMC.open(BMARK_PATH, FILE_WRITE);
  if (!f) return;
  f.print(String(currentChunk) + ":" + String((unsigned long)pageIndex));
  f.close();
}

// ── OLED update ───────────────────────────────────────────────────────────────
static void updateOLED() {
  if (chunks.empty() || currentChunk >= (int)chunks.size()) return;
  int totalCk = (int)chunks.size();
  int maxPg   = getMaxPage();

  float progress =
      (totalCk <= 1)
          ? (maxPg > 0 ? (float)pageIndex / (float)maxPg : 1.0f)
          : ((float)currentChunk + (maxPg > 0 ? (float)pageIndex / (float)maxPg : 1.0f)) /
                (float)totalCk;
  progress = constrain(progress, 0.0f, 1.0f);
  int barFill = (int)(253.0f * progress);

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);

  String title = chunks[currentChunk].heading;
  if (title.length() == 0) title = "Chunk " + String(currentChunk + 1);
  if ((int)title.length() > 36) title = title.substring(0, 35) + "~";
  u8g2.drawStr(1, 9, title.c_str());

  String info = "Pg " + String((unsigned long)(pageIndex + 1)) + "/" + String(maxPg + 1) +
                "  Ck " + String(currentChunk + 1) + "/" + String(totalCk);
  u8g2.drawStr(1, 20, info.c_str());

  u8g2.drawFrame(0, 25, 256, 7);
  if (barFill > 0) u8g2.drawBox(1, 26, min(barFill, 254), 5);

  u8g2.sendBuffer();
}

// ── Document rendering ────────────────────────────────────────────────────────
static int renderSourceLine(int si, int startX, int startY) {
  const SourceLine& src   = s_sourceLines[si];
  char              style = src.style;

  // Early-out: entire source line is before the scroll window
  if (src.lineCount > 0 &&
      s_displayLines[src.lineStart + src.lineCount - 1].scrollIdx < lineScroll)
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
    if (dl.scrollIdx < lineScroll) continue;

    int      cx      = drawX;
    uint16_t max_hpx = 0;

    // First pass: measure tallest glyph on this display line
    for (int wi = dl.wordStart; wi < dl.wordStart + dl.wordCount; wi++) {
      const WordRef& w = s_wordRefs[wi];
      display.setFont(pickFont(style, w.bold, w.italic));
      int16_t  x1, y1;
      uint16_t wpx, hpx;
      display.getTextBounds(w.text, cx, cursorY, &x1, &y1, &wpx, &hpx);
      if (hpx > max_hpx) max_hpx = hpx;
    }
    if (style == '1' || style == '2' || style == '3') max_hpx += 4;

    // Second pass: render
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

  // Post-render decorations
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
  lineScroll = pageIndex * LINES_PER_PAGE;
  int cursorY = startY;
  for (int si = 0; si < s_sourceLinesUsed; si++) {
    if (cursorY >= display.height() - 6) break;
    cursorY += renderSourceLine(si, startX, cursorY);
  }
}

// ── Entry points ──────────────────────────────────────────────────────────────
void APP_INIT() {
  initFonts();
  fontStyle    = serif;
  fileError    = false;
  currentChunk = 0;
  pageIndex    = 0;

  buildOrLoadIndex();
  if (!fileError) {
    loadBookmark();  // may update currentChunk and pageIndex
    loadChunk(currentChunk);
    int mp = getMaxPage();
    if ((int)pageIndex > mp)
      pageIndex = (ulong)mp;
  }
  needsRedraw = true;
}

void processKB_APP() {
  char ch = KB().updateKeypress();
  if (!ch) return;

  // ESC or A: save bookmark and return to OS
  if (ch == 27 || ch == 65) {
    saveBookmark();
    rebootToPocketMage();
    return;
  }

  // Modifier toggles
  if (ch == 17) {  // SHIFT
    KB().setKeyboardState(KB().getKeyboardState() == SHIFT ? NORMAL : SHIFT);
    return;
  }
  if (ch == 18) {  // FN
    KB().setKeyboardState(KB().getKeyboardState() == FUNC ? NORMAL : FUNC);
    return;
  }

  if (ch == 21) {  // RIGHT arrow (normal) — next page
    if ((int)pageIndex < getMaxPage()) {
      pageIndex++;
      needsRedraw = true;
    } else if (currentChunk + 1 < (int)chunks.size()) {
      currentChunk++;
      pageIndex = 0;
      saveBookmark();
      ESP.restart();
    }

  } else if (ch == 19) {  // LEFT arrow (normal) — prev page
    if (pageIndex > 0) {
      pageIndex--;
      needsRedraw = true;
    } else if (currentChunk > 0) {
      currentChunk--;
      pageIndex = 65535;  // sentinel: APP_INIT clamps to getMaxPage()
      saveBookmark();
      ESP.restart();
    }

  } else if (ch == 6) {  // RIGHT arrow (FN) — next chunk
    if (currentChunk + 1 < (int)chunks.size()) {
      currentChunk++;
      pageIndex = 0;
      saveBookmark();
      ESP.restart();
    }
    KB().setKeyboardState(NORMAL);

  } else if (ch == 12) {  // LEFT arrow (FN) — prev chunk
    if (currentChunk > 0) {
      currentChunk--;
      pageIndex = 65535;  // sentinel: APP_INIT clamps to getMaxPage()
      saveBookmark();
      ESP.restart();
    }
    KB().setKeyboardState(NORMAL);

  } else if (KB().getKeyboardState() == FUNC || KB().getKeyboardState() == FN_SHIFT) {
    // Unrecognized key in FN mode — clear FN state so arrows don't stay broken
    KB().setKeyboardState(NORMAL);
  }
}

void einkHandler_APP() {
  if (!needsRedraw) return;
  needsRedraw = false;

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  if (fileError || chunks.empty() || currentChunk >= (int)chunks.size()) {
    display.setFont(&FreeSerif9pt7b);
    display.setCursor(10, 30);
    display.print(fileError ? "Cannot open:" : "No content found");
    if (fileError) {
      display.setCursor(10, 50);
      display.print(BOOK_PATH);
    }
    EINK().refresh();
    return;
  }

  // Header: chunk heading + separator rule
  display.setFont(&Font5x7Fixed);
  String header = chunks[currentChunk].heading;
  if (header.length() == 0) header = "Chunk " + String(currentChunk + 1);
  if ((int)header.length() > 44) header = header.substring(0, 43) + "~";
  display.setCursor(4, 11);
  display.print(header);
  display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

  // Body content
  renderDocument(4, CONTENT_START_Y);

  EINK().refresh();
  updateOLED();
}
