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
