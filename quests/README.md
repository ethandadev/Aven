# Contributor quests

Small, guided ways to help make Aven better. The editor lists them under
**Help > Contributor Quests**, walks through the steps, opens the right files and
checks your work.

Each quest is one JSON file:

| key | meaning |
| --- | --- |
| `id` | short name, same as the file name |
| `title`, `summary`, `why` | what it is and why it matters |
| `level` | `Starter` (no code), `Easy` (JSON or EasyScript), `Medium` (C++) |
| `time` | a rough guess, like `20 minutes` |
| `touches` | what kind of files you change |
| `learn` | what you'll learn along the way |
| `steps` | each has `text`, and optionally `open` (a file to open) and `check` |
| `submit` | how to send the change |

Checks tick steps automatically:

- `{"type": "json_valid", "path": "editor/data/doctor_rules.json"}`
- `{"type": "files", "path": "templates/*/template.json", "min": 9}`
- `{"type": "contains", "path": "…", "text": "…"}`
- `{"type": "project_files", "path": "bug_reports/*/report.md", "min": 1}` (in the open game)

Paths are relative to Aven's source folder. Writing a new quest is itself a good
first contribution!
