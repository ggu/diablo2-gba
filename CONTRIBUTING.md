# Contributing

The default original-art prototype is the simplest contribution path. It needs no Diablo II installation, ROM, demo, extracted assets, reference checkout, or emulator. Start with the setup in [README](README.md), check tools with `./dev doctor`, then run:

```sh
./dev build
./dev test
```

Also run `python3 tools/check_source_tree.py` to verify that no private game
data or generated payloads entered the source tree. These local checks are the
default verification path. GitHub Actions is disabled in the repository
settings, and its source workflow is manual-only. Enabling it, dispatching it,
or adding automatic triggers requires an explicit request. Local verification
of the proposed commit is sufficient for review; do not wait for a redundant
Actions run.

Install mGBA only when you want to play the default game: `./dev play diablo2`. See [the game guide](diablo2/README.md) for controls and mechanics.

The optional Den profile can be prepared with `./dev den prepare`. It downloads and verifies the pinned free demo, then converts source content into ignored local files. Keep all demo archives, extracted data, converted assets, generated headers, ROMs, saves, screenshots, and emulator reports in ignored `.local/` or `build/` directories. Do not commit or distribute those materials. No demo or proprietary content is required for ordinary contributions. See [the Den guide](docs/D2-DEN.md) for its source boundary and supported workflow.

Project-authored files are licensed under the GNU General Public License, version 3 or any later version (`GPL-3.0-or-later`); see [LICENSE](LICENSE). Contributions to project-authored files are offered under the same terms. Preserve existing copyright and license notices. External code and data keep their own terms; see [third-party notices](THIRD_PARTY_NOTICES.md). Do not contribute code or content you lack permission to distribute under its applicable terms.
