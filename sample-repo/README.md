# Sample libget repo (for hpak)

This folder is a minimal **libget "get" repository** that hpak can consume.

It includes one example package: **Bad Apple!! (Switch)** (`bad-apple-switch`), packaged from:
- https://github.com/souldbminerr/bad-apple-switch/releases/download/bad_apple_ounce/bad-apple-switch.nro

## Structure

```text
sample-repo/
  repo.json                         # package index
  packages/
    bad-apple-switch/
      icon.png                      # package icon (served by GetRepo)
  zips/
    bad-apple-switch.zip            # downloadable package payload
```

`zips/bad-apple-switch.zip` contains:
- `manifest.install` (`U: switch/bad-apple-switch/bad-apple-switch.nro`)
- `info.json`
- `switch/bad-apple-switch/bad-apple-switch.nro`

## Hosting

Serve this directory over HTTP(S) so these URLs resolve from the repo base URL:
- `<repo-url>/repo.json`
- `<repo-url>/zips/bad-apple-switch.zip`
- `<repo-url>/packages/bad-apple-switch/icon.png`

Examples:

```bash
# local test server (from this folder)
python -m http.server 8000
# repo URL: http://<your-pc-ip>:8000
```

You can also host `sample-repo/` via GitHub Pages or any static file host.

## Add to hpak

hpak reads libget repos from:
- `sdmc:/switch/hpak/.get/repos.json`

Add a repo entry (example):

```json
{
  "repos": [
    {
      "name": "Sample Repo",
      "url": "http://<your-host>:8000",
      "enabled": true,
      "type": "get"
    }
  ]
}
```

> If `repos.json` already exists, add the object to the existing `repos` array instead of replacing the file (unless you intend to).

Then open **Sources → Refresh repositories** in hpak. The package should appear in **Home/Apps**, and installing it will place the NRO at:
- `sdmc:/switch/bad-apple-switch/bad-apple-switch.nro`

## Notes

- This is a standard libget **get** repo layout (`repo.json` + `zips/<name>.zip`). A direct GitHub release `.nro` URL is not used as the download endpoint by GetRepo; the NRO is packaged into the repo zip instead.
- `sample-repo/pkgbuild/` contains the downloaded source NRO used to build the zip (kept for convenience/reproducibility). You can regenerate the zip from it if needed.
- For larger/public repos, consider generating metadata/zips with libget's `repogen.py` workflow and adding accurate `updated` timestamps/checksums.

## Package fields (hpak extensions + dependencies)

Versions are compared numerically with 1–4 segments (`x`, `x.x`, `x.x.x`, `x.x.x.x`);
a leading `v` and trailing suffixes (e.g. `-ounce`) are ignored, so `1.0` equals
`1.0.0`. Non-numeric versions (e.g. `bad_apple_ounce`) only match by exact string.

Updates are directional: a remote version only shows as an update when it is
strictly *newer* than the installed one — an older repo version never offers a
downgrade, for regular and direct-download packages alike.

Direct-download packages (installed straight from a release, no zip):

```json
{
  "name": "my-app",
  "file": "my-app.nro",
  "direct_url": "https://github.com/you/my-app/releases/latest/download",
  "path": "sdmc:/switch/",
  "UpdateDateTime": "23/11/2026 19:00"
}
```

- `file`: release asset name. A `.zip` asset is extracted to `sdmc:/` (replacing files); otherwise the file is placed in `path` (default `sdmc:/switch/`, created if needed).
- `path`: install directory for single-file downloads.
- Direct installs write a standard registry entry (`packages/<name>/manifest.install` with `U: <sd-relative-path>` lines plus `info.json` carrying the full metadata) — the same layout as regular packages, so install state, versions and update checks all go through libget's registry. Installed state is resolved from the manifest alone: no manifest means not installed, even if payload files exist.
- `UpdateDateTime`: `"DD/MM/YYYY HH:MM"` (24hr, UTC) — powers Featured “recently updated” sorting. The repo updater script stamps this automatically on version changes.
- If `repo.json` already carries `title`, `author`, `version`, `description`, `details`, `license` and `changelog`, no per-release `package.json` fetch is needed; otherwise hpak reads `<direct_url>/package.json` (same fields, multiline `details` supported) and `<direct_url>/icon.png` (`.jpg`/`.jpeg` also tried).

Dependencies (regular and direct packages alike):

```json
{
  "name": "my-app",
  "depends": ["other-lib"],
  "dependencies": { "other-tool": "1.2.x" }
}
```

- `depends`: array of package names (any version satisfies).
- `dependencies`: object mapping package name → version constraint. Constraints are 1–4 numeric segments with optional `x`/`*` wildcards (`"1.2.x"`, `"2"`); missing segments act as a prefix (`"1.0"` accepts `1.0.x`). `"*"` (or empty) accepts anything.
- Dependencies install automatically before the package (transitively, with cycle detection); already-installed ones are skipped. Missing or version-mismatched dependencies abort the install with an error naming the package.
- Dependents are shown in the app details (“Requires: …”).
- Removing a package is blocked while an installed package depends on it (`Cannot remove 'x': required by 'y'`). Afterwards, hpak offers to remove dependencies left unused by anything installed (“Would you like to remove unused dependencies?”).

Deletion lists (both optional arrays of `sdmc:/...` paths, in `repo.json` and/or `package.json`):

```json
{
  "name": "my-app",
  "requiredDeletion": ["sdmc:/switch/my-app/"],
  "userDeletion": ["sdmc:/switch/my-app/saves/"]
}
```

- `requiredDeletion`: always deleted on uninstall. Provide it for anything the normal payload removal misses — for single-file apps this is optional, for ZIP apps list the directories to clean (extracted files are covered by the manifest, but empty/runtime-created dirs are not).
- `userDeletion`: user data (saves, configs). If non-empty, uninstalling prompts first: “Would you like to remove this package's data completely? This may include saves, configurations, etc.” with Remove all / Keep data.
- Deletion safety: only `sdmc:/` paths without `..` are honored, symlinks are never traversed, `sdmc:/hbmenu.nro` is explicitly protected, and root-level directories (`sdmc:/switch`, `sdmc:/Nintendo`, `sdmc:/kaihen`, …) can never be deleted — only paths at least two levels deep (`sdmc:/switch/<dir>/`). Single files may live at any depth.
- Both fields also sync from `package.json` via the repo updater scripts.
