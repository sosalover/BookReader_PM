// Book Reader OTA App — PocketMage
// Place your .md file at /books/book.md on the SD card.
// Navigation: < / > to page through, FN+< / FN+> to jump chapters.
// Press ESC to save your position and return to PocketMage OS.

#include <SD_MMC.h>
#include <globals.h>

#include <vector>

// ── Configuration ─────────────────────────────────────────────────────────────
static const char* const BOOK_PATH = "/books/book.md";
static const char* const BMARK_DIR = "/books/.bmarks";
static const char* const BMARK_PATH = "/books/.bmarks/book.bmark";

#define SPECIAL_PADDING 20
#define SPACEWIDTH_SYMBOL "n"
#define WORDWIDTH_BUFFER 0
#define DISPLAY_WIDTH_BUFFER 14
#define HEADING_LINE_PADDING 8
#define NORMAL_LINE_PADDING 4
#define LINES_PER_PAGE 12
#define CONTENT_START_Y 20

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
  fonts[serif].normal = &FreeSerif9pt7b;
  fonts[serif].normal_B = &FreeSerifBold9pt7b;
  fonts[serif].normal_I = &FreeSerif9pt7b;  // italic fallback
  fonts[serif].normal_BI = &FreeSerifBold9pt7b;
  fonts[serif].h1 = &FreeSerif12pt7b;
  fonts[serif].h1_B = &FreeSerif12pt7b;
  fonts[serif].h2 = &FreeSerifBold9pt7b;
  fonts[serif].h2_B = &FreeSerifBold9pt7b;
  fonts[serif].h3 = &FreeSerif9pt7b;
  fonts[serif].h3_B = &FreeSerifBold9pt7b;
  fonts[serif].code = &FreeMonoBold9pt7b;
  fonts[serif].quote = &FreeSerif9pt7b;
  fonts[serif].list = &FreeSerif9pt7b;
}

static const GFXfont* pickFont(char style, bool bold, bool italic) {
  FontMap& fm = fonts[fontStyle];
  switch (style) {
    case '1':
      return bold ? fm.h1_B : fm.h1;
    case '2':
      return bold ? fm.h2_B : fm.h2;
    case '3':
      return bold ? fm.h3_B : fm.h3;
    case 'C':
      return fm.code;
    case '>':
      return fm.quote;
    case '-':
      return fm.list;
    case 'L':
      return fm.list;
    default:
      if (bold && italic)
        return fm.normal_BI;
      if (bold)
        return fm.normal_B;
      if (italic)
        return fm.normal_I;
      return fm.normal;
  }
}

// ── Data structures ───────────────────────────────────────────────────────────
struct wordObject {
  String text;
  bool bold = false;
  bool italic = false;
};

struct LineObject {
  ulong index;
  std::vector<wordObject> words;
};

static ulong indexCounter = 0;
static ulong lineScroll = 0;

struct DocLine {
  char style = 'T';
  String line;
  std::vector<wordObject> words;
  std::vector<LineObject> lines;
  ulong orderedListNumber = 0;

  void parseWords() {
    words.clear();
    int i = 0;
    while (i < (int)line.length()) {
      if (line[i] == '*' && i + 1 < (int)line.length() && line[i + 1] == '*') {
        int end = line.indexOf("**", i + 2);
        if (end < 0)
          end = line.length();
        splitIntoWords(line.substring(i + 2, end), true, false);
        i = end + 2;
      } else if (line[i] == '*') {
        int end = line.indexOf("*", i + 1);
        if (end < 0)
          end = line.length();
        splitIntoWords(line.substring(i + 1, end), false, true);
        i = end + 1;
      } else {
        int nextBold = line.indexOf("**", i);
        int nextItalic = line.indexOf("*", i);
        int end = line.length();
        if (nextBold >= 0)
          end = min(end, nextBold);
        if (nextItalic >= 0)
          end = min(end, nextItalic);
        splitIntoWords(line.substring(i, end), false, false);
        i = end;
      }
    }
  }

  void splitToLines() {
    uint16_t textWidth = (uint16_t)(display.width() - DISPLAY_WIDTH_BUFFER);
    if (style == '>' || style == 'C')
      textWidth -= SPECIAL_PADDING;
    else if (style == '-' || style == 'L')
      textWidth -= 2 * SPECIAL_PADDING;

    lines.clear();
    LineObject current;
    int lineWidth = 0;

    for (auto& w : words) {
      display.setFont(pickFont(style, w.bold, w.italic));
      int16_t x1, y1;
      uint16_t wpx, hpx, sw, sh;
      display.getTextBounds(w.text.c_str(), 0, 0, &x1, &y1, &wpx, &hpx);
      display.getTextBounds(SPACEWIDTH_SYMBOL, 0, 0, &x1, &y1, &sw, &sh);
      int addWidth = (int)wpx + (int)sw + WORDWIDTH_BUFFER;

      if (lineWidth > 0 && (lineWidth + addWidth > (int)textWidth)) {
        current.index = indexCounter++;
        lines.push_back(current);
        current.words.clear();
        lineWidth = 0;
      }
      current.words.push_back(w);
      lineWidth += addWidth;
    }
    if (!current.words.empty()) {
      current.index = indexCounter++;
      lines.push_back(current);
    }
  }

  // Returns pixel height used
  int displayLine(int startX, int startY) {
    ulong scroll = lineScroll;
    int cursorY = startY;

    if (!lines.empty() && lines.back().index < scroll)
      return 0;

    if (style == 'H') {
      display.drawFastHLine(0, cursorY + 3, display.width(), GxEPD_BLACK);
      display.drawFastHLine(0, cursorY + 4, display.width(), GxEPD_BLACK);
      return 8;
    }
    if (style == 'B')
      return 12;

    int drawX = startX;
    if (style == '>')
      drawX += SPECIAL_PADDING;
    else if (style == '-' || style == 'L')
      drawX += 2 * SPECIAL_PADDING;
    else if (style == 'C')
      drawX += SPECIAL_PADDING / 2;

    for (auto& ln : lines) {
      if (ln.index < scroll)
        continue;

      int cx = drawX;
      uint16_t max_hpx = 0;

      for (auto& w : ln.words) {
        display.setFont(pickFont(style, w.bold, w.italic));
        int16_t x1, y1;
        uint16_t wpx, hpx;
        display.getTextBounds(w.text.c_str(), cx, cursorY, &x1, &y1, &wpx, &hpx);
        if (hpx > max_hpx)
          max_hpx = hpx;
      }
      if (style == '1' || style == '2' || style == '3')
        max_hpx += 4;

      for (auto& w : ln.words) {
        display.setFont(pickFont(style, w.bold, w.italic));
        int16_t x1, y1;
        uint16_t wpx, hpx, sw, sh;
        display.getTextBounds(w.text.c_str(), cx, cursorY, &x1, &y1, &wpx, &hpx);
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
      String num = String(orderedListNumber) + ". ";
      display.setFont(pickFont('T', false, false));
      int16_t x1, y1;
      uint16_t wpx, hpx;
      display.getTextBounds(num.c_str(), 0, 0, &x1, &y1, &wpx, &hpx);
      display.setCursor(drawX - (int)wpx - 5, startY + (int)hpx);
      display.print(num.c_str());
    }

    return cursorY - startY;
  }

 private:
  void splitIntoWords(const String& seg, bool bold, bool italic) {
    int start = 0;
    while (start < (int)seg.length()) {
      int sp = seg.indexOf(' ', start);
      if (sp < 0)
        sp = seg.length();
      String word = seg.substring(start, sp);
      if (word.length() > 0) {
        wordObject wo;
        wo.text = word;
        wo.bold = bold;
        wo.italic = italic;
        words.push_back(wo);
      }
      start = sp + 1;
    }
  }
};

// ── Chapter tracking ──────────────────────────────────────────────────────────
struct Chapter {
  size_t offset;  // byte position in file where this chapter starts
  String title;
};

static std::vector<Chapter> chapters;
static std::vector<DocLine> docLines;
static int currentChapter = 0;
static ulong pageIndex = 0;
static bool needsRedraw = true;
static bool fileError = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
static int getTotalDisplayLines() {
  int n = 0;
  for (const auto& d : docLines)
    n += (int)d.lines.size();
  return n;
}

static int getMaxPage() {
  int total = getTotalDisplayLines();
  return (total <= 0) ? 0 : (total - 1) / LINES_PER_PAGE;
}

// ── Chapter scanning ──────────────────────────────────────────────────────────
static void scanChapters() {
  chapters.clear();
  File f = SD_MMC.open(BOOK_PATH, FILE_READ);
  if (!f) {
    fileError = true;
    return;
  }

  while (f.available()) {
    size_t pos = f.position();
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("# ")) {
      chapters.push_back({pos, line.substring(2)});
    }
  }
  f.close();

  // No chapter markers — treat whole file as one section
  if (chapters.empty()) {
    chapters.push_back({0, "Book"});
  }
}

// ── Chapter loading ───────────────────────────────────────────────────────────
static void populateDocLines() {
  indexCounter = 0;
  for (auto& d : docLines) {
    d.parseWords();
    d.splitToLines();
  }
  if (docLines.empty()) {
    DocLine blank;
    blank.style = 'T';
    blank.line = "(empty)";
    docLines.push_back(blank);
    docLines.back().parseWords();
    docLines.back().splitToLines();
  }
}

static void loadChapter(int idx) {
  if (idx < 0 || idx >= (int)chapters.size())
    return;

  File f = SD_MMC.open(BOOK_PATH, FILE_READ);
  if (!f) {
    fileError = true;
    return;
  }

  f.seek(chapters[idx].offset);
  size_t endOffset = (idx + 1 < (int)chapters.size()) ? chapters[idx + 1].offset
                                                      : (size_t)0;  // 0 signals "read until EOF"

  docLines.clear();
  ulong listCounter = 1;

  while (f.available()) {
    if (endOffset != 0 && f.position() >= endOffset)
      break;

    String raw = f.readStringUntil('\n');
    raw.trim();

    char st = 'T';
    String content = raw;

    if (raw.length() == 0)
      st = 'B';
    else if (raw == "---")
      st = 'H';
    else if (raw.startsWith("# ")) {
      st = '1';
      content = raw.substring(2);
    } else if (raw.startsWith("## ")) {
      st = '2';
      content = raw.substring(3);
    } else if (raw.startsWith("### ")) {
      st = '3';
      content = raw.substring(4);
    } else if (raw.startsWith("> ")) {
      st = '>';
      content = raw.substring(2);
    } else if (raw.startsWith("- ")) {
      st = '-';
      content = raw.substring(2);
      listCounter = 1;
    } else if (raw.startsWith("```")) {
      st = 'C';
      content = "";
    } else if (raw.length() >= 3 && isDigit(raw[0]) && raw[1] == '.' && raw[2] == ' ') {
      st = 'L';
      content = raw.substring(3);
    }

    DocLine dl;
    if (st == 'L')
      dl.orderedListNumber = listCounter++;
    else
      listCounter = 1;
    dl.style = st;
    dl.line = content;
    docLines.push_back(dl);
  }
  f.close();

  populateDocLines();
  needsRedraw = true;
}

// ── Bookmarks ─────────────────────────────────────────────────────────────────
static void loadBookmark() {
  if (!SD_MMC.exists(BMARK_PATH))
    return;
  File f = SD_MMC.open(BMARK_PATH, FILE_READ);
  if (!f)
    return;
  String data = f.readStringUntil('\n');
  f.close();

  int sep = data.indexOf(':');
  if (sep < 0)
    return;

  int ch = data.substring(0, sep).toInt();
  ulong pg = (ulong)data.substring(sep + 1).toInt();

  if (ch >= 0 && ch < (int)chapters.size()) {
    currentChapter = ch;
    pageIndex = pg;
  }
}

static void saveBookmark() {
  if (!SD_MMC.exists(BMARK_DIR))
    SD_MMC.mkdir(BMARK_DIR);
  File f = SD_MMC.open(BMARK_PATH, FILE_WRITE);
  if (!f)
    return;
  f.print(String(currentChapter) + ":" + String((unsigned long)pageIndex));
  f.close();
}

// ── OLED update ───────────────────────────────────────────────────────────────
static void updateOLED() {
  int totalCh = (int)chapters.size();
  int maxPg = getMaxPage();

  float progress =
      (totalCh <= 1)
          ? (maxPg > 0 ? (float)pageIndex / (float)maxPg : 1.0f)
          : ((float)currentChapter + (maxPg > 0 ? (float)pageIndex / (float)maxPg : 1.0f)) /
                (float)totalCh;
  progress = constrain(progress, 0.0f, 1.0f);
  int barFill = (int)(253.0f * progress);

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);

  // Chapter title (truncated to ~36 chars)
  String title = chapters[currentChapter].title;
  if ((int)title.length() > 36)
    title = title.substring(0, 35) + "~";
  u8g2.drawStr(1, 9, title.c_str());

  // Page / chapter info
  String info = "Pg " + String((unsigned long)(pageIndex + 1)) + "/" + String(maxPg + 1) + "  Ch " +
                String(currentChapter + 1) + "/" + String(totalCh);
  u8g2.drawStr(1, 20, info.c_str());

  // Progress bar at bottom
  u8g2.drawFrame(0, 25, 256, 7);
  if (barFill > 0)
    u8g2.drawBox(1, 26, min(barFill, 254), 5);

  u8g2.sendBuffer();
}

// ── Document rendering ────────────────────────────────────────────────────────
static void renderDocument(int startX, int startY) {
  lineScroll = pageIndex * LINES_PER_PAGE;
  int cursorY = startY;
  for (auto& doc : docLines) {
    if (cursorY >= display.height() - 6)
      break;
    cursorY += doc.displayLine(startX, cursorY);
  }
}

// ── Entry points ──────────────────────────────────────────────────────────────
void APP_INIT() {
  initFonts();
  fontStyle = serif;
  fileError = false;
  currentChapter = 0;
  pageIndex = 0;

  scanChapters();
  if (!fileError) {
    loadBookmark();  // may update currentChapter and pageIndex
    loadChapter(currentChapter);
    int mp = getMaxPage();
    if ((int)pageIndex > mp)
      pageIndex = (ulong)mp;
  }
  needsRedraw = true;
}

void processKB_APP() {
  char ch = KB().updateKeypress();
  if (!ch)
    return;

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

  bool isFN = (KB().getKeyboardState() == FUNC || KB().getKeyboardState() == FN_SHIFT);

  if (ch == '>') {
    if (isFN) {
      // Jump to next chapter
      if (currentChapter + 1 < (int)chapters.size()) {
        currentChapter++;
        pageIndex = 0;
        loadChapter(currentChapter);
      }
      KB().setKeyboardState(NORMAL);
    } else {
      // Next page, or advance to next chapter if on last page
      if ((int)pageIndex < getMaxPage()) {
        pageIndex++;
        needsRedraw = true;
      } else if (currentChapter + 1 < (int)chapters.size()) {
        currentChapter++;
        pageIndex = 0;
        loadChapter(currentChapter);
      }
    }

  } else if (ch == '<') {
    if (isFN) {
      // Jump to previous chapter
      if (currentChapter > 0) {
        currentChapter--;
        pageIndex = 0;
        loadChapter(currentChapter);
      }
      KB().setKeyboardState(NORMAL);
    } else {
      // Prev page, or go to last page of previous chapter
      if (pageIndex > 0) {
        pageIndex--;
        needsRedraw = true;
      } else if (currentChapter > 0) {
        currentChapter--;
        loadChapter(currentChapter);
        pageIndex = (ulong)getMaxPage();
        needsRedraw = true;
      }
    }
  }
}

void einkHandler_APP() {
  if (!needsRedraw)
    return;
  needsRedraw = false;

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  if (fileError) {
    display.setFont(&FreeSerif9pt7b);
    display.setCursor(10, 30);
    display.print("Cannot open:");
    display.setCursor(10, 50);
    display.print(BOOK_PATH);
    EINK().refresh();
    updateOLED();
    return;
  }

  // Header: chapter title + separator rule
  display.setFont(&Font5x7Fixed);
  String header = chapters[currentChapter].title;  //
  if ((int)header.length() > 44)
    header = header.substring(0, 43) + "~";
  display.setCursor(4, 11);
  display.print(header);
  display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

  // Body content
  renderDocument(4, CONTENT_START_Y);

  EINK().refresh();
  updateOLED();
}
