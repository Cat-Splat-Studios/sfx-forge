# wiki/

The pages of SFX FORGE's wiki, kept in the repository and published to the GitHub wiki by a script.

The reference documents ([README](../README.md), [the manual](../docs/MANUAL.md),
[the Unity manual](../docs/UNITY.md), [the maintainer's guide](../docs/MAINTAINING.md)) are long and complete.
The wiki is the easy way in: many short pages, each about one thing, with a picture or a diagram, and a link to
the exact section of the reference for the rest.

Start reading at [Home.md](Home.md) or [Start-Here.md](Start-Here.md). On GitHub's view of this folder the links
between pages do not work, because they are written for the wiki (`[text](Page-Name)`, without `.md`); the
diagrams and pictures do.

## What is here

| | |
|---|---|
| `*.md` | the pages, flat. The file name is the page name with hyphens |
| `Home.md`, `_Sidebar.md`, `_Footer.md` | the front page, the list of all pages, the foot line |
| `img/` | every picture the pages show. Some are copies of `docs/img`, the others are made by `figures/` |
| `figures/` | the scripts that make the wiki's own pictures. Not published |
| `README.md` | this file. Not published |

## Edit, check, publish

```sh
python tools/check_wiki.py                 # every link, anchor, picture and diagram; exit code 1 on a failure
python tools/check_wiki.py --sync          # first copy again the pictures that changed in docs/img
python tools/publish_wiki.py --repo-url https://github.com/OWNER/REPO
```

Run them from the repository's root. They need Python 3; the checker also asks git which files are ignored.

`python tools/check_wiki_code.py --compile` compiles the C# samples of the pages in Unity (batch mode, on
`tests/unity-inspectors`); `--clean` removes what it generated. The page below says how a sample is written for it.

`publish_wiki.py` runs the checker, and when it passes writes `out/wiki-publish/`: the pages and `img/`, with every
`../` link turned into an address of the repository (`--branch`, default `main`, names the branch they point
into). It pushes nothing and runs no git command. It prints the three git commands that publish the folder; the
owner runs them. The first time, the wiki's own repository (`<repo>.wiki.git`) must exist and be cloned into
`out/wiki-publish`: the script says how.

Do not edit pages in GitHub's wiki editor. The next publish overwrites them.

The conventions (how a page reads, links, pictures, Mermaid, the truth rule) and the steps to add a page or a
picture are on the page [Editing-This-Wiki.md](Editing-This-Wiki.md).

## When a feature changes

Document it in the reference first, then here: change the wiki page that teaches it, or add one and put it in
`_Sidebar.md` and the glossary. Update `What-Is-Tested.md` when a test run or its numbers change. Then run the
checker.
