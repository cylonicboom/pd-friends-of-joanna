# Friends of Joanna v0.3.1 Basedir

This repository is the working basedir for **Perfect Dark: Friends of Joanna** (`v0.3.1`).

It is intended for:

- maintaining local config/save artifacts used by the mod layout
- syncing main repo changes
- publishing `data/mods` as a subtree to a dedicated modconfig repository (eg allow swapping between foj-only, foj+AIO and foj+Goldeneye:X layouts)

## Repository role

The main development repo is:

- `pd-friends-of-joanna-basedir`

The modconfig subtree target (default) is:

- `https://github.com/cylonicboom/pd-friends-of-joanna-modconfig`

`tools/sync` exists to standardize this sync/publish workflow.

## Quick start

From repository root:

```bash
tools/sync setup
tools/sync push
```

What each command does:

`setup`:

- ensures remote `pd-friends-of-joanna-modconfig` exists
- updates that remote URL if it differs from expected

`push`:

- detects the git project root
- enters root with `pushd` inside a subshell for atomic directory state
- runs `git push`
- runs `git subtree push --prefix=data/mods <remote> <branch>`
- exits and restores directory state automatically

## Overrides

The script supports env-var overrides for remote and branch:

- `REMOTE_NAME`
- `REMOTE_URL`
- `SUBTREE_BRANCH`

Examples:

```bash
REMOTE_NAME=my-modconfig tools/sync setup
REMOTE_URL=https://github.com/yourname/pd-friends-of-joanna-modconfig tools/sync setup
SUBTREE_BRANCH=fojo-v0.3.1 tools/sync push
```

`SUBTREE_PREFIX` is intentionally pinned to `data/mods`.

## Assumptions

- your default `git push` target is configured correctly for this repo
- you have push access to both the main repo remote and subtree target remote
- subtree history for `data/mods` is managed through this workflow

## Notes on omitted PD+/jpn-final files

The following files are intentionally not included:

- `aio:CheadgreyZ|CheadMikadoZ|jpn-final:CheadDark.*Z`
- `0c58.bin`
- `0c59.bin`
- `0c5a.bin`
- `0c5b.bin`

A future release will pull Japanese Joanna straight from the `jpn-final` rom if you have it.