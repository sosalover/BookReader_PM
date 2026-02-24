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
| `FN + <` | Previous chunk |
| `FN + >` | Next chunk |
| `b` | Save bookmark & return to book picker |
| `A` | Save bookmark & return to OS |


---

# Development Notes

- Chunk-based loading prevents memory crashes
- It's a little hacky that we keep restarting, but it' a result of mem.

Some todos:

2. Page jump shortcut
3. Hide chunks — user sees only pages
4. Suppress the PM startup beep on ESP.restart() chunk nav
5. User documentation
6. Add page move with the scroller

