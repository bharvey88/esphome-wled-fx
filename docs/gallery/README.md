# The effect gallery

A static page showing an animated preview of every effect `wled_fx` registers,
with the parameters each effect takes and a copy-ready YAML snippet for both
front ends. Plain HTML, CSS and JavaScript: no framework, no build step at
serve time, nothing loaded from anywhere else and nothing tracked.

```
docs/gallery/
  index.html      the page
  gallery.css     the styles
  gallery.js      search, filters, lazy loading, the detail panel
  effects.json    generated: every effect, its flags, its defaults, its labels
  previews/       generated: <effect>.webp animated, <effect>.still.webp poster
```

## What the previews are, and are not

The engine's frame, exactly as the simulator renders it, with no output gamma.
A real panel applies gamma 2.2 after this, as WLED does in `show()` and as the
display front end now does by default, so hardware looks more contrasty than
the gallery: a preview pixel at 128 is 56 on the panel.

Applying that curve here was considered and rejected. A dozen sparse effects
have a mean brightness under 5 of 255, and gamma 2.2 would put them under 1,
which is a black thumbnail in a catalogue whose job is to let somebody pick an
effect. The page says which it is showing instead.

## Look at it locally

Anything that serves static files will do, as long as it is a server: the page
reads `effects.json` with `fetch`, which a browser refuses to do over `file://`.

```
python -m http.server 8731 --directory docs/gallery
```

Then open [localhost:8731](http://localhost:8731). Stop the server with Ctrl
and C.

## Regenerate it

```
python tools/gallery/build_gallery.py
```

That configures and builds the simulator into `tools/sim/build-gallery`, renders
every registered effect, encodes the previews and rewrites `effects.json`. It
needs `cmake`, `ninja`, a C++17 compiler and `ffmpeg` with libwebp on PATH, and
it takes about a minute on a normal laptop. Raw frames go to a temporary
directory and are deleted; they are never committed.

Useful flags: `--sim PATH` to reuse a simulator you already built, `--only NAME`
(repeatable) to re-render one effect while you are choosing its palette, and
`--jobs N` to change how many effects are rendered at once.

What the previews are: 100 frames at 20 frames per second, five seconds, looped,
on a 64 by 32 panel for anything that runs in 2D and on a 64 pixel strip for
anything that only runs in 1D. Every effect runs on its own metadata defaults.
The slow effects in the simulator's `PACING` table keep the same time
acceleration they get in the verification run, and the audio reactive effects
run on WLED's simulated sound, exactly as they do on a device with no
microphone. Six effects are previewed on a palette rather than palette 0,
because palette 0 means "use the segment colours" and leaves them as a flat
amber field; `PALETTE_OVERRIDE` at the top of `build_gallery.py` is the list,
and the page says so on each of those effects.

The previews are encoded at the canvas resolution and the page does the
upscaling, with `image-rendering: pixelated` and a round LED mask in CSS. That
keeps a preview at roughly 30 KB rather than several hundred, and it stays crisp
at any card width instead of being sharp at one size only.

## Publishing it

`.github/workflows/gallery.yml` rebuilds the gallery and deploys it to GitHub
Pages on a push to `main` that touches the effects, the simulator, the gallery
tool or the page itself, and on a manual run. A manual run on a branch builds
the gallery and uploads it as an ordinary artifact without deploying.

Nothing is published yet. Pages is off, and `actions/deploy-pages` fails rather
than skipping when it is off, so the two Pages steps are behind a repository
variable and are skipped until somebody with admin rights turns all of it on,
in this order:

1. Settings, then Pages, then under **Build and deployment** set **Source** to
   **GitHub Actions**.
2. Settings, then Environments: open **github-pages** and, if it has a
   deployment branch rule, make sure `main` is allowed.
3. Settings, then Secrets and variables, then Actions, then the **Variables**
   tab: add a repository variable named `GALLERY_PAGES` with the value `true`.
   This is what unskips the publish. Do it after the first two, not before.
4. Actions, then **Effect gallery**, then **Run workflow** on `main` to publish
   the first copy. After that every qualifying push does it.

Until step 3, the workflow still builds the gallery on every qualifying push
and uploads it as the `gallery` artifact, so a broken page is still caught. It
just does not publish, and it stays green while it does not.

The site then serves from the URL Pages shows on that settings page, and the
workflow prints it as the deployment URL.
