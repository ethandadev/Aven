# Contributing to Aven

Thanks for helping. Aven is for beginners, so contributions that make something clearer count as
much as new features.

## The easiest way in: Contributor Quests

The editor has guided tasks under **Help > Contributor Quests**. Each one explains why it
matters, opens the right files, and checks your work as you go. Many need no C++ at all:

| Quest | You'll change |
|---|---|
| Report a bug (with Bug Replay) | nothing: you write the report |
| Teach the Error Doctor a new error | `editor/data/doctor_rules.json` |
| Teach Ask Aven new words | `editor/data/assistant_words.json` |
| Write a tutorial for a template | `templates/*/tutorial.json` |
| Make a template | a new folder in `templates/` |
| Test on the web | nothing: you test and report |
| Improve the Code Ladder | `engine/src/aven/script/translate*.cpp` |
| Add a behavior | `engine/src/aven/scene/components.*` and `runtime/behaviors.cpp` |
| Add a script function | `engine/src/aven/runtime/script_system.cpp` |
| Write a quest | `quests/*.json` (see [quests/README.md](quests/README.md)) |

## Building and testing

See [docs/getting-started.md](docs/getting-started.md) for what to install. Then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/aven_tests                      # all tests
./build/bin/aven_tests translate            # only tests whose name contains "translate"
```

The editor can drive itself, which is handy for checking UI changes and on machines without a
screen:

```sh
xvfb-run -a ./build/bin/aven-editor templates/platformer --screenshot out.png --frames 30 --panel doctor
```

To check the web player, build it with `tools/web/build_web_player.sh` (it needs Emscripten).

If you change what scripts can call, regenerate the API reference with
`tools/docs/make_api_reference.sh`.

## Style

- **Match the code around you.** C++20; format with the repository's `.clang-format` (4 spaces,
  110 columns).
- **Write for beginners.** Every message the user sees is plain English: say what happened and
  what to do next. Prefer "Can't find the image 'hero.png'. Check that the file exists in your
  project folder." over "texture load failed".
- **Comment why, not what.** Keep comments short.
- **Add a test** for engine behavior you add or fix. `tests/` has examples for scripts, physics,
  translation, replay and native modules.
- **No new dependencies without discussion.** Everything Aven uses is fetched by CMake and listed
  in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Pull requests

1. Fork, and make a branch for your change.
2. Keep each pull request to one change, and say what it does and how you checked it. A
   screenshot helps for editor changes.
3. Make sure `aven_tests` passes. CI builds on Linux, Windows, macOS and the web, and runs the
   tests.

By contributing, you agree your work is released under the [MIT License](LICENSE).

## Making a release

Releases are built by `.github/workflows/release.yml`. To publish one from `main`:

```sh
git tag v0.2.0
git push origin v0.2.0
```

That builds and tests Aven on Windows, macOS and Linux, packs each into a zip with the templates
and the web player (`tools/release/package.sh`), checks the Linux zip runs on its own, and
publishes a GitHub release with the zips and notes generated from the commits. The tag sets the
version the editor shows. A tag with a dash, like `v0.2.0-beta.1`, becomes a pre-release.

To try the build without publishing, open the Release workflow on the Actions tab and click
**Run workflow**: the zips are attached to that run instead.
