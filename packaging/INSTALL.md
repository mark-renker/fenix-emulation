# Installing Fenix I

No compiling needed. Download an installer from the
[Releases page](https://github.com/mark-renker/fenix-emulation/releases).

| File | What it is |
|---|---|
| `FenixI-<version>-macOS.pkg` | macOS 11+ (Apple Silicon and Intel): **Fenix I** (AU, VST3, CLAP, standalone app) and **Fenix I FX** (AU, VST3, CLAP) |
| `FenixI-<version>-Windows-x64.zip` | Windows 10/11: VST3, CLAP and the standalone `.exe` |
| `FenixI-<version>-macOS-bundles.zip` | The raw macOS bundles plus `topology_params.json`, if you'd rather copy them by hand |

## macOS

1. Download the `.pkg` and double-click it.
2. **If macOS says it "can't verify the developer"**, that's expected. The installer
   is not notarized with a paid Apple Developer ID.
   - **macOS 15 (Sequoia) and later:** click **Done**, open **System Settings →
     Privacy & Security**, scroll down to the message about the installer, click
     **Open Anyway**, and confirm with your password.
   - **macOS 11–14:** right-click (or Control-click) the `.pkg`, choose **Open**,
     then **Open** again.
3. Follow the installer. On the **Installation Type** screen you can pick
   which plug-in and which formats to install.
4. Restart your DAW, or rescan plug-ins. Both plug-ins are listed under
   **Renky Denk**: *Fenix I* (instrument) and *Fenix I FX* (effect).

You only have to approve the installer once. Plug-ins installed by it load
normally, with no further security prompts.

**Where things go:** `/Library/Audio/Plug-Ins/Components` (AU),
`/Library/Audio/Plug-Ins/VST3` (VST3), `/Library/Audio/Plug-Ins/CLAP` (CLAP),
`/Applications` (standalone app).

**Uninstall:** delete `Fenix I.component`, `Fenix I FX.component`, `Fenix I.vst3`,
`Fenix I FX.vst3`, `Fenix I.clap`, `Fenix I FX.clap` from those folders and
`Fenix I.app` from `/Applications`.

**Copying bundles by hand (from the `-bundles.zip`):** put the `.component` files in
`~/Library/Audio/Plug-Ins/Components`, the `.vst3` in `~/Library/Audio/Plug-Ins/VST3`
and the `.clap` in `~/Library/Audio/Plug-Ins/CLAP`. Files downloaded this way are
quarantined by macOS, so clear that once in Terminal:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Fenix I.component" \
        ~/Library/Audio/Plug-Ins/VST3/"Fenix I.vst3" ~/Library/Audio/Plug-Ins/CLAP/"Fenix I.clap"

(and the same for the `Fenix I FX` bundles).

## Windows

1. Unzip `FenixI-<version>-Windows-x64.zip`.
2. Copy the `.vst3` folders to `C:\Program Files\Common Files\VST3\` and the `.clap`
   files to `C:\Program Files\Common Files\CLAP\`.
3. Rescan plug-ins in your DAW.
4. The standalone `Fenix I.exe` runs from anywhere. If Windows SmartScreen warns
   about an unrecognized app, click **More info → Run anyway**. The app isn't code-signed.

## Using it

- **Nothing sounds until it is patched** (like the hardware). Open **PATCHES** and
  pick one, then play MIDI; the rear strip carries MIDI 1 V/oct, gate, velocity and
  mod wheel as patchable outputs.
- **Patching:** drag from any socket to another to add a cable. Black = input,
  green = audio output, blue = CV output. Several cables into one input sum;
  tying two outputs together makes them "fight" like the real banana system.
  Right-click a socket to remove its cables; **CLEAR** removes all.
- **Knobs:** drag up/down (Shift = fine), double-click to reset, scroll wheel to nudge.
- **Retuning the circuit model:** copy `topology_params.json` to
  `~/Library/Application Support/FenixI/` (Windows: `%APPDATA%\FenixI\`), edit it,
  press **RELOAD PARAMS**. The status line at the bottom confirms what was loaded.

## For maintainers: making a release

- **Locally (macOS):** `packaging/make_installer.sh --build` builds a universal
  binary and writes `dist/FenixI-<version>-macOS.pkg` + the bundles zip.
  Without `--build` it packages whatever is in `build/` (pass another build dir
  as an argument).
- **In the cloud:** push a tag (`git tag v0.1.1 && git push origin v0.1.1`).
  `.github/workflows/release.yml` builds the universal macOS installer and the
  Windows zip, runs the harness (`--quick`, CPU budget informational on shared
  runners), and publishes a GitHub Release. **Run workflow** in the Actions tab
  does the same builds as artifacts only.
- **Notarized builds (optional):** with an Apple Developer ID, set
  `DEVELOPER_ID_APP`, `DEVELOPER_ID_INSTALLER` and `NOTARY_PROFILE` before running
  `make_installer.sh`; it then signs with the hardened runtime, notarizes and staples.
