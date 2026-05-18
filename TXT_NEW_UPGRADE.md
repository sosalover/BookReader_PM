# TXT_NEW Upgrade Plan — Static Layout Pool

## Background

TXT_NEW (`src/OS_APPS/TXT_NEW.cpp`) is the built-in markdown editor app. It was written before the book reader OTA app, which developed a zero-heap static-pool rendering pipeline under severe memory constraints. This document captures the lessons learned and the proposed changes to TXT_NEW.

---

## Architectural Comparison

| Concern | Book Reader (APP_TEMPLATE.cpp) | TXT_NEW |
|---|---|---|
| **Word storage** | `const char*` into `s_textPool[10240]` (BSS) | `String text` in `wordObject` — heap per word |
| **Layout output** | `s_displayLines[600]` static array (BSS) | `std::vector<LineObject> lines` inside each `DocLine` — heap per wrap |
| **Source lines** | `s_sourceLines[100]` static array (BSS) | `std::vector<DocLine> docLines` — heap per line |
| **Document scope** | 100-line chunks; `ESP.restart()` on chunk nav | Entire file loaded at once |
| **Rendering** | Procedural: pools → `renderSourceLine()` | OOP: `DocLine::displayLine()` method |
| **Populate pass** | One-pass: parse + layout in `layoutSourceLine()` | Two-pass: `parseWords()` then `splitToLines()` |
| **Font system** | 1 family (serif), simplified FontMap (13 pointers) | 3 families (serif/sans/mono), 28 pointers each = 84 total |
| **OLED** | Status bar: heading + Pg/Ck + progress bar | Rich: miniature doc preview, editor toolbar, line width indicator |
| **Ordered lists** | Inline `listCounter` during parse | Separate `refreshOrderedListIndexes()` pass |
| **Editing** | Read-only | Full append/inline editor, BKSP, style cycling, save/load/new |

---

## What Cannot Be Ported

The book reader stores words as `const char*` pointers into an immutable pool — suitable for **read-only** content. TXT_NEW needs mutable words (characters added/removed in `editAppend()`, words split/merged on Space/Enter/Backspace). So `internWord()` / `s_textPool` **cannot replace `wordObject::String text`**.

The `ESP.restart()` chunk-nav trick also cannot apply to an editor (whole document must stay in memory).

---

## Proposed Changes (Priority Order)

### 1. Static Layout Pool — replace `std::vector<LineObject> lines` in DocLine

**Problem:** `DocLine::splitToLines()` pushes `LineObject` structs (each containing a `std::vector<wordObject>`) onto a heap vector. Every re-layout (on any edit) causes many small heap allocs that fragment over time.

**Fix:** Keep `std::vector<wordObject> words` in `DocLine` (mutable, needed for editing). Replace `std::vector<LineObject> lines` with a global static array.

```cpp
// Add at file scope:
#define MAX_LAYOUT_LINES 1200  // 300-line note × ~4 wraps avg = 1200 display lines (~9.6 KB BSS)

struct LayoutLine {
    uint16_t docLineIndex;   // index into docLines
    uint16_t wordStart;      // index into docLines[docLineIndex].words
    uint8_t  wordCount;
    ulong    scrollIdx;
};

static LayoutLine s_layout[MAX_LAYOUT_LINES];
static int        s_layoutUsed = 0;
static ulong      s_layoutCounter = 0;
```

Changes to `DocLine::splitToLines()`:
- Instead of `lines.push_back(currentLine)`, write to `s_layout[s_layoutUsed++]`
- Store `docLineIndex` so `displayLine()` can find words
- Remove `std::vector<LineObject> lines` from the struct entirely

Changes to `DocLine::displayLine()`:
- Iterate `s_layout` entries where `docLineIndex == this_index` instead of `this->lines`

Changes to `displayDocument()`:
- Can iterate `s_layout` directly in scroll-index order

Changes to `populateLines()`:
- Reset `s_layoutUsed = 0; s_layoutCounter = 0;` before the loop

**Heap savings:** On a 100-DocLine note, current code allocates ~100 `std::vector<LineObject>` + ~400 `std::vector<wordObject>` inside them. With static pool, that's ~5 KB of heap allocs → 0 heap allocs for the layout pass. The per-word `wordObject::String text` heap stays (needed for editing) but the wrapping layer is now free.

### 2. One-pass Layout with Per-Line Dirty Flag (medium effort)

Currently `populateLines()` re-processes all DocLines on every `updateScreen = true`. Only the edited line actually needs re-layout.

Add `bool dirty` to `DocLine`. Set `dirty = true` when the line is modified in `editAppend()`. In `populateLines()`, skip lines where `!dirty` and reuse their existing `s_layout` entries. Requires tracking `s_layout` ranges per DocLine (store `layoutStart`, `layoutCount` in `DocLine` or in a parallel array).

More complex — implement after #1 is working.

### 3. Inline Ordered-List Counter (trivial)

Remove `refreshOrderedListIndexes()` separate pass. In the `populateLines()` loop:

```cpp
int listCounter = 0;
for (auto& doc : docLines) {
    if (doc.style == 'L') doc.orderedListNumber = ++listCounter;
    else { doc.orderedListNumber = 0; listCounter = 0; }
    // ... layout ...
}
```

### 4. Keep TXT_NEW's Font System As-Is

TXT_NEW's 3-family FontMap is already superior to the book reader's simplified version. Do not regress it.

---

## Verification

1. `pio run -e PM_V3` — no compile errors
2. On device: open TXT_NEW via OS → load an existing note → render is correct
3. Type headings, lists, code blocks, bold/italic — layout pool fills correctly
4. Backspace through words and lines — pool resets on re-layout, no stale entries
5. Long note (100+ lines): check `ESP.getFreeHeap()` before/after edit — should be higher than pre-patch
6. Save → reload → content unchanged (editing pipeline unaffected)
