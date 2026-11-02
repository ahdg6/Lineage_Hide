# Configuration v1

`/data/adb/lineage_hide/config.conf` is ASCII data, never executable shell. Maximum 64 KiB. Blank lines and lines starting with `#` are allowed. CRLF imports are accepted; writes normalize to LF. No whitespace trimming is performed by the native parser. The UI trims each list entry.

Every scalar below is required exactly once:

```ini
schema=1
profile=lineage
enabled=1
mode=all
services=1
features=1
broadcasts=1
resources=1
reflection=1
force_unmount=1
legacy_boot=0
native_paths=1
```

`profile` is `lineage`, `evox` or `custom`: a descriptive label, not an instruction to merge hidden defaults. `mode` is `all`, `include` or `exclude`. Booleans accept only `0` or `1`. Unknown keys, duplicate scalars and invalid enum values are rejected.

Repeat these keys for lists; omitted lists are empty, never implicitly inherited:

| Key | Matching / constraints |
| --- | --- |
| `package` | Exact package; up to 512 entries; applies across users |
| `service_keyword` | Case-insensitive substring, normalized lowercase; >= 3 characters |
| `exact_service` | Case-insensitive exact service name; also permits `/`, `:` and `@` |
| `feature` | Case-sensitive exact feature name |
| `broadcast` | Case-sensitive exact action |
| `resource_package` | Case-sensitive exact resource package name |
| `resource_id` | Two lowercase hex digits; `00`, `01`, `7f` forbidden |
| `path_keyword` | Case-insensitive filename substring, lowercase normalized; >= 3 characters |
| `hide_path` | Explicit file in the five partition roots; max 512 entries |
| `map_path` | Explicit mapping file in the same roots; max 512 entries |

Package and explicit path lists accept up to 512 entries each; other lists are limited to 128 entries. Names are at most 255 ASCII characters, using letters, digits, `.`, `_`, `-`, with the exact-service exceptions above. Package names require valid dot-separated identifier segments and do not permit `-`. Duplicate entries are rejected; duplicate case variants of service keywords are rejected after normalization. There are no wildcards or regular expressions. Resource names/IDs should be verified against the ROM before use.

## CLI

From a root shell, `CTL=/data/adb/modules/lineage_hide/bin/configctl`:

```sh
"$CTL" get                      # validated canonical configuration
"$CTL" preset lineage           # print defaults, do not save
"$CTL" preset evox
"$CTL" validate "$(cat draft.conf)"  # validate without saving
"$CTL" set "$(cat draft.conf)"       # validate + atomic save
"$CTL" status                   # package snapshot readiness / boot-script state
"$CTL" init lineage             # initialize ONLY if absent; preserve existing settings
"$CTL" legacy-boot              # exit 0 only when enabled && legacy_boot
```

The WebUI safely shell-quotes the complete argument. Validation is native, so bypassing JavaScript does not bypass bounds or schema checks. The helper requires root on Android. Host tests compile `LH_HOST_TEST` with isolated temporary state/runtime paths; production builds never enable it.

`native_paths` is the independent kernel-path switch (on in both presets). Explicit paths are at most 255 bytes, use `/` plus the name characters above, and must start in `/system/`, `/system_ext/`, `/product/`, `/vendor/` or `/odm/`. Empty, `.` and `..` components are rejected. Only existing regular files with no symlink components may be applied. Missing files appear in the preview; mapping lists are separate from hidden paths.

Additional commands:

```sh
"$CTL" path-plan                  # read-only plan from saved configuration
"$CTL" path-plan "$(cat draft.conf)" # read-only plan from draft
"$CTL" apply-paths                # register saved rules; normally boot service only
```

The WebUI never calls `apply-paths`; saving only stores settings. Status and logs describe the latest attempt, not a verified app view. See [SUSFS backend](SUSFS-BACKEND.md) for version gates and partial-application semantics.

The companion reads configuration and the volatile system-package list afresh for each eligible process, through bounded length-prefixed frames and socket timeouts. Missing, invalid, truncated or oversized data skips injection. PackageManager must successfully enumerate system packages and include the Android framework package before the snapshot is published. The snapshot is generated at boot; changes to system package classification require a reboot.

No live hooks are removed from an existing app. Save, then force-stop and reopen every affected package (all users/processes that matter). Library changes need a device reboot. `enabled=0` stops future injection and prevents native path registration and the legacy script on the next boot, but does not undo changes already made by that script.
