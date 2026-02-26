# Book Reader (PocketMage app)

A lightweight Markdown reader for PocketMage that loads `.md` files from the SD card and renders them on the e-ink display with paging, chunking, and persistent bookmarks.

This is a fork of Ashtf's PocketMage project. The code for my OTA_APP is in Code/PocketMage_V3/src. I included the .bin files there and the .tar file.



---

# 🧭 How To Use

## Convert Books to .md
This reader uses .md. You can convert from .epub to .md with this link: https://nellowtcs.me/MiniRepos/ePub2Markdown/index.html

## Add Books

Create a folder on your SD card (using the USB app):

`/books/`

Add one or more `.md` files:


- `/books/thisIsABook.md`
- `/books/book2.md`

---

## Launch the App

Install the OTA app, then launch it from PocketMage OS.

On startup:

- If **one book exists**, it opens automatically.
- If **multiple books exist**, the book picker appears.

---

## Book Picker Controls

| Key | Action |
|------|--------|
| `<` | Previous book |
| `>` | Next book |
| `Space` / `Enter` | Open selected book |
| `ESC` | Exit to PocketMage OS |

---

## Reading Controls

You need a bookmark to save your spot. Every few pages, your bookmark will update automatically. There are also controls below for bookmarking when you're done reading.

Bookmarks are stored in:


`/books/.bmarks/`


On relaunch, the reader resumes from your bookmark.

| Key | Action |
|------|--------|
| `<` | Previous page |
| `>` | Next page |
| `FN + <` | Previous chunk (section) |
| `FN + >` | Next chunk (section) |
| `g` | Jump to page — type a number, confirm with `Space` / `Enter`, cancel with `ESC` |
| `b` / `B` | Save bookmark & return to book picker |
| `A` / `ESC` | Save bookmark & return to OS |
| Touch strip | Swipe right → next page, swipe left → previous page |

---

# Development Notes

- Chunk-based loading prevents memory crashes — the device restarts between chunks to keep the heap clean.
- Global page numbers (e.g. "Pg 42/380") shown on OLED once the index is built; cached to SD so it only runs once per book.

Some todos:

3. Hide chunks — user sees only pages
4. Suppress the PM startup beep on chunk nav restart

