# Friends of Joanna Modconfig Subtree
This directory is the subtree payload published from the basedir repository's `data/mods` path.


`fojo-.*` branches contain the modconfig layout consumed by the Friends of Joanna basedir. 
`main` contains the deprecated prototyping AIO+fojo layout. 

In the future other mod layout combinations will be added.  

## Repository Role
Within the basedir repository, this directory is the source for:

- `git subtree push --prefix=data/mods ...`
- the external `pd-friends-of-joanna-modconfig` repository
- the mod payload that should travel independently of the rest of the basedir

This is the publishable modconfig slice, not the full runtime basedir.

## Current Layout
Current top-level structure:

```text
data/mods/
	mod_fojo/
	ext_tex/
	files/
	textures/
	modconfig.txt
```

`mod_fojo` is the canonical Friends of Joanna mod package in this subtree.

## Editing Guidance
Keep this subtree focused on files that belong in the published modconfig payload.

Good candidates:

- `modconfig.txt`
- mod-specific files under `files/`
- mod-specific textures under `textures/`
- external texture payloads under `ext_tex/` when needed by the mod

Avoid putting unrelated basedir-only state here, such as:

- save data
- local emulator/runtime artifacts
- top-level repo tooling unrelated to the subtree payload

## Sync Workflow
This subtree is normally published from the basedir repository root with:

```bash
tools/sync push
```

That script:
- pushes the main repository
- pushes `data/mods` as a subtree to the configured modconfig remote

If needed, you can override the subtree target when running the script:

```bash
REMOTE_NAME=my-modconfig tools/sync setup
REMOTE_URL=https://github.com/yourname/pd-friends-of-joanna-modconfig tools/sync setup
SUBTREE_BRANCH=fojo-v0.3.1 tools/sync push
```

## Design Note
Earlier versions of this documentation described a broader multi-mod layout. The current subtree is intentionally narrower and documents the layout that actually ships from this basedir.
