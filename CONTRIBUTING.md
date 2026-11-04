# Contributing to Satchel

Thanks for helping. Satchel is a small research project with one maintainer and a lot of
unexplored ground, so almost any careful contribution is useful.

## Ground rules

- **No Nintendo material.** Never commit keys, firmware, game files, decrypted code,
  prepared packages, shader dumps or anything derived from a game. Do not paste them into
  issues or logs. Issues asking where to get games or keys are closed.
- **No GPL code.** Reuse MIT, BSD, Apache or zlib code with its notice intact. Port
  Horizon services from Ryujinx (MIT) rather than writing them from scratch when they
  exist there. Ask before bringing in anything with another license.
- **Report what you verified.** Say whether something was built, ran on a phone, or was
  played, and for how long. "Builds" and "plays" are different claims.
- **Do not weaken a check to make it pass.** If a verifier rejects something, fix the
  cause or explain the failure in the pull request.
- **Writing style.** No em dashes or double hyphens as punctuation anywhere (UI copy,
  docs, comments, commit messages). `scripts/check_repo_hygiene.sh` enforces this and
  runs in CI.

## Ways to help

- **Compatibility reports.** The cheapest and most valuable contribution. Use the
  compatibility report issue template and attach the game report from Prepare, Game
  diagnostics. See [COMPATIBILITY.md](COMPATIBILITY.md).
- **Bugs.** Use the bug report template. Include the phone model, iOS version, the
  Satchel commit, and the steps.
- **Code.** Pick something from the "help wanted" list in [ROADMAP.md](ROADMAP.md) or
  propose your own. Open an issue first for anything larger than a fix, so two people do
  not do the same work.
- **Docs.** If a guide was wrong or missing a step for you, fix it.

## Pull requests

1. Branch from `main`. Keep one topic per pull request.
2. Run the checks that apply (see [docs/testing.md](docs/testing.md)) and
   `sh scripts/check_repo_hygiene.sh`.
3. In the description: what changed, what you verified and how (exact commands), what
   you could not verify, and the next blocker if there is one.
4. If the change touches a game's behavior, say which game and version you tested and on
   what phone.

The maintainer builds and installs on the phone before merging engine changes, so a
pull request that compiles but was not run on a device is still welcome; just say so.

## Building and testing

See [BUILDING.md](BUILDING.md) for the (currently maintainer-specific) build and
[docs/testing.md](docs/testing.md) for the test scripts, the simulator UI suite and the
Mac harness.

## Automated workers

Parts of Satchel were written by coding agents working from `AGENTS.md`. If you use one,
point it at that file; its rules apply to agents and humans alike.
