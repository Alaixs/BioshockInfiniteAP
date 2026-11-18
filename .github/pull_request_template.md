## What and why

<!-- What the change does and the problem it solves. Link the issue if there is one. -->

## Testing

- [ ] `.\build.ps1 -Lint` passes and the build has no warning
- [ ] `.\build.ps1 -Test` passes (needed when the client, the apworld or a mod command changed)
- [ ] `python apworld/run_tests.py <Archipelago checkout>` passes (needed when the apworld changed)
- [ ] Tested in game: <!-- which part of the game, what you checked; or "not needed" and why -->
- [ ] `world_version` bumped if generation or slot_data changed; no item or location id changed
