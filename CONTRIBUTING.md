# Contributing

Thank you for looking. This page says how to build and test a change, under which licence a contribution is taken,
and the sentence to add to a pull request.

## Build and test

Everything is in [the maintainer's guide](docs/MAINTAINING.md): the map of the code, what to install, every command
and what it must print. The wiki has the same as short pages:
[Building From Source](wiki/Building-From-Source.md), [Running The Tests](wiki/Running-The-Tests.md),
[Release Checklist](wiki/Release-Checklist.md).

The short form, from the repository's root:

```sh
sh build.sh                                          # the firmware and its checks: 26 lines say PASSED
sh native/build.sh                                   # only if core/ or native/ changed: the native library
dotnet run -c Release --project tests/dotnet -- .    # the package's core without Unity: the last line is PASSED
python tools/check_wiki.py                           # after a change to a wiki page or to a heading it links to
```

The Unity tests (EditMode on `tests/unity-inspectors`, the showcase's checks and PlayMode tests) are in
[the maintainer's guide, section 2](docs/MAINTAINING.md).

Before you open a pull request:

- Run what your change can break, and say in the pull request what you ran and what you did not.
- Run the formatters ([the maintainer's guide, "The formatters"](docs/MAINTAINING.md#the-formatters)).
- No machine path and no e-mail address in a file.
- Do not edit `.tools/` or the generated package repository: change the source or `tools/make_unity_package.py`.
- Say where anything comes from that you did not write yourself, and under which licence it is.

## The licence of a contribution

SFX FORGE is under the GNU General Public License, version 3 or any later version (`GPL-3.0-or-later`), with the
additional permissions of [ADDITIONAL-PERMISSIONS.md](ADDITIONAL-PERMISSIONS.md): see [LICENSE](LICENSE) and
[README, Licence](README.md#licence). A contribution is accepted under the same licence.

The copyright holder, Cat Splat Studios, also offers SFX FORGE under other terms to those who cannot use the GPL,
for example for a game that is not offered under it. That is only possible when the copyright holder may license
every part of the project that way. So a contributor is asked to allow it, and keeps the copyright in the
contribution.

Add this sentence to your pull request, with your name:

> I wrote this contribution myself, or I have the right to submit it. I license it under GPL-3.0-or-later with the
> additional permissions of ADDITIONAL-PERMISSIONS.md, and I grant Cat Splat Studios a perpetual, worldwide,
> non-exclusive, royalty-free, irrevocable licence to use, change and distribute it, and to license it to others
> under other terms as well, including terms that are not open source.

A pull request without the sentence is not merged. If you cannot agree to it, open an issue and describe the change
instead: someone else may write it.

A new file among siblings that carry the line `SPDX-License-Identifier: GPL-3.0-or-later` gets that line too. No
licence block is written into a file.
