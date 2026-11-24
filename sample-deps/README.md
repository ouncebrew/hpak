# Sample dependency server (foo depends on bar)

Minimal repo to exercise hpak's dependency install end to end. Both packages
are direct downloads that drop a TXT on the **SD root**:

- `bar` → `sdmc:/bar.txt`
- `foo` → `sdmc:/foo.txt`, with `"dependencies": { "bar": "1.0.x" }`

## Serve it

From this folder, replacing `192.0.2.1` with your PC's LAN IP first
(the Switch must reach it over your network):

```bash
# Linux/macOS/MSYS
grep -rl '192.0.2.1' . | xargs sed -i 's/192.0.2.1/<your-pc-ip>/g'
python -m http.server 8000
# repo URL: http://<your-pc-ip>:8000
```

```powershell
# PowerShell
Get-ChildItem -Recurse -File | Select-String '192.0.2.1' | Select-Object -ExpandProperty Path -Unique | ForEach-Object { (Get-Content $_) -replace '192.0.2.1','<your-pc-ip>' | Set-Content $_ }
python -m http.server 8000
```

## Test in hpak

1. **Sources → Add source** the repo URL above, then refresh.
2. Open **foo** — details should show `Requires: bar (1.0.x)`.
3. **Install foo** — progress runs through the dependency first, then foo.
   Afterwards both `sdmc:/bar.txt` and `sdmc:/foo.txt` exist.
4. Try to **remove bar** — blocked with `Cannot remove 'bar': required by 'foo'`.
5. **Remove foo** — since foo lists `userDeletion`, you get the
   “remove data completely?” prompt first (Keep data is pre-selected).
   Afterwards you’re asked about unused dependencies: answering Yes
   removes the now-orphaned `bar` too; answering No leaves it.
   Either way `sdmc:/foo.txt` is gone, `requiredDeletion` paths like
   `sdmc:/bar_data/` are always cleaned, and removal never cascades
   implicitly. With No, `bar` stays installed and can be removed on its own.

To test a constraint failure, bump foo's constraint to something bar can't
satisfy (e.g. `"bar": "2.x"`) and reinstall — the install aborts naming `bar`.
