# HPK
HPK, or Horizon Package, is a file format that hpak uses.

HPKs are really just ZIP files, and can be easily extracted with a zip tool.

They are formatted as so

/ example.hpk
  meta.json
  sdout/
    [place files to copy to SD card here]

meta.json is formatted like so:

{
    "dependencies": {
        "sdl3": "HPK_Major"
        "miniz": "HPK_Any"
    }
}