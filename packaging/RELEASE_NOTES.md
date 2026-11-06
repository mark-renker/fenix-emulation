Circuit-level emulation of the Synton Fénix I: all 31 modules, banana patching with
feedback and output stacking, 2x/4x oversampled engine, every inferred circuit constant
in `topology_params.json`, and a measurement harness (65 checks) that ships with the build.

**Downloads**

- `FenixI-<version>-macOS.pkg` — macOS 11+ installer (Apple Silicon + Intel): Fenix I (AU, VST3, CLAP, standalone) and Fenix I FX (AU, VST3, CLAP). Unsigned: see `packaging/INSTALL.md` for the one-time Gatekeeper step.
- `FenixI-<version>-macOS-bundles.zip` — the raw bundles, to copy by hand.
- `FenixI-<version>-Windows-x64.zip` — VST3, CLAP and standalone for Windows 10/11.

Validated with `auval` and pluginval (strictness 8) on both plug-ins; the harness report
is in `docs/VERIFICATION.md`.
