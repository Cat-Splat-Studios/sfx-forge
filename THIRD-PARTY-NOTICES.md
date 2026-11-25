# Third-party notices

SFX FORGE's own code, documents and pictures are Cat Splat Studios' work under the GNU General Public License,
version 3 or any later version ([LICENSE](LICENSE), with [ADDITIONAL-PERMISSIONS.md](ADDITIONAL-PERMISSIONS.md)).
Everything you hear is synthesized by that code: there are no samples or recordings.

The parts below are other people's. They stay under their own licences, and their licence and notice texts are in
[`licenses/`](licenses), copied unchanged.

## VIBE FM SDK

- Project: <https://github.com/Vibe-FM-SDK/vibe-fm-sdk>, by Sven Trogus (zvenson)
- Licence: Apache License 2.0: [`licenses/VIBE-FM-SDK-LICENSE-Apache-2.0.txt`](licenses/VIBE-FM-SDK-LICENSE-Apache-2.0.txt)
- The SDK's NOTICE file, as section 4(d) of that licence asks:
  [`licenses/VIBE-FM-SDK-NOTICE.txt`](licenses/VIBE-FM-SDK-NOTICE.txt)

Where it is: no source file of the SDK is in this repository. The SDK's build compiles its runtime
(`sdk/src/vibe_sdk.c`) and its display fonts (`sdk/src/vibe_font.c`) into every firmware, so they are in the built
`sfx-forge.wasm` and `sfx-forge.zip` of a release, in compiled form and unchanged. The device in the two device
videos is drawn by the SDK's emulator, and one video shows the emulator's page itself.

SFX FORGE is built with the VIBE FM SDK. "VIBE FM" is a trademark of Sven Trogus.

## The display fonts

The SDK's fonts are glyph bitmaps rendered from fonts under the SIL Open Font License 1.1:
[`licenses/VIBE-FM-SDK-LICENSE-FONTS-OFL-1.1.txt`](licenses/VIBE-FM-SDK-LICENSE-FONTS-OFL-1.1.txt). They are in the
built firmware and in the videos' pictures of the display.

## The C runtime in the native library

`unity/com.catsplat.sfxforge/Runtime/Plugins/x86_64/sfxforge.dll` is built with Zig and holds start-up code of the
mingw-w64 C runtime: [`licenses/mingw-w64-COPYING.txt`](licenses/mingw-w64-COPYING.txt),
[`licenses/Zig-LICENSE.txt`](licenses/Zig-LICENSE.txt).
