# Native OpenVPN 3 editor

This plugin integrates `org.freedesktop.NetworkManager.openvpn3` into the
Plasma **Widgets connection editor** and its VPN authentication dialog. It
requires the companion NetworkManager OpenVPN 3 backend with the secret-profile
contract below. It does not add an editor to `libs/editorqml` or implement
OpenVPN core features such as PKCS#11. Unsupported directives remain editable
and preserved; preserving them does not make the backend support them.

General fields, the ordered directives table, and Profile Source share one
profile document. Unknown directives, duplicate entries, comments, quoting,
connection blocks and untouched line endings are preserved. File references
and inline credentials entered in the editor go through the real libnm backend
importer before saving. When returning from a source/table view, successful
normalization becomes the current document, so later credential edits win and
embedded files no longer depend on the originals. Relative references typed
in Source have no original file directory; use absolute paths or **Embed File**.
Importing an existing file resolves its relative paths through the backend.

Reimport replaces VPN configuration and credentials, preserving VPN timeout,
persistent and user-name properties. The host editor preserves the original
connection properties (including interface binding) before applying deliberate
UI edits, and owns identity, permissions, IP settings and routes. Reimport confirms unsaved edits or replacement of stored
credentials; cancellation and import failure leave the editor unchanged. The
current profile and password storage choices are respected. Export is refused
because the profile can contain private keys.

## Secret storage

| Choice | `vpn.data` | `vpn.secrets` |
| --- | --- | --- |
| Wallet (new/imported default) | `profile-storage=secret`, `profile-flags=1` | `profile=base64(UTF-8 profile)` |
| Explicit system storage | `profile-storage=secret`, `profile-flags=0` | Same profile secret, owned by NetworkManager |
| Existing legacy connection only | `profile=base64(UTF-8 profile)` | Other secrets as before |

New and secret-backed connections cannot select public legacy storage. An
existing legacy connection can retain it with an explicit unprotected-storage
label. Secret mode never reads or writes a public fallback. Locked, corrupt or
unsupported profiles block saving until replaced or made available.

Wallet availability requires a connected session bus and a registered or
activatable Secret Service/KWallet provider, as well as QtKeychain capability.
`QKeychain::isAvailable()` alone is insufficient: its libsecret path can report
true when there is no bus. See the [QtKeychain backend implementation](https://github.com/frankosterfeld/qtkeychain/blob/main/qtkeychain/keychain_unix.cpp).
The check does not write secrets or prove that unlocking/writing will succeed.
The existing Plasma NetworkManager secret agent handles storage and reports
write errors through its existing SaveSecrets error path. Cancellation, provider
failure after validation, and `saveSecretsWithoutReply` paths are not a new
transactional persistence guarantee.

A separate real KWallet probe verified an 8,172-byte synthetic secret map through
write, independent read and daemon restart. Its cancellation checks **failed**:
unlock cancellation (also after a wrong password) returned QtKeychain `NoError`
with empty data. That suite finished **56 assertions passed, 2 failed**, not
all-green. The plugin treats an empty/missing profile reply as unavailable when
locked, preserves the original map, and cannot save an empty replacement. If a
valid profile was already loaded, an empty reply leaves it intact. Delayed
credential replies also preserve newly edited text and storage policies.
No upstream QtKeychain changes are included. See the
[final targeted verification record](../../.hermes/results/openvpn3-final-targeted-summary.md)
for independent wallet and backend e2e evidence and their limits.

The editor blocks wallet saves when that prerequisite is absent. KCM's direct
import path is also gated because it immediately adds the imported connection.
To use system storage without a wallet, create an OpenVPN 3 connection, choose
system profile/password storage explicitly, and import from its editor.
OpenVPN 3 explicitly initializes both PasswordField policies to user storage;
the shared PasswordField's availability-dependent default cannot downgrade a
new profile's passwords silently. Existing explicit per-password choices remain
visible and are preserved.

The editor drops old `challenge-response` values even from blocked output.
Authentication only returns a freshly entered challenge, never an old OTP.
Initial no-hints requests with an unavailable profile explain the problem.
Explicit password/passphrase/challenge retry hints still work without the
profile, which NetworkManager may omit for RequestNew retries.

## Reproduction

The repository owns the Dockerfiles and runner in [`testing/`](testing/).
Only the image-build command installs packages, inside Docker. Build/test runs
have no network, no host bus/home/socket mounts, no real credentials and no
host package or service changes. The test image installs the actual companion
backend libnm importer. Its test UID must match the invoking user so private
D-Bus EXTERNAL authentication has an NSS identity.

```bash
export BUILD_ROOT=/home/tom/.hermes/cache/scratch/ovpn3-build
export BACKEND_ROOT=/home/tom/git/manicminer/network-manager-openvpn3
vpn/openvpn3/testing/run.sh images        # optional when the images already exist
vpn/openvpn3/testing/run.sh build -j4
vpn/openvpn3/testing/run.sh test -R openvpn3 -V
vpn/openvpn3/testing/run.sh test
vpn/openvpn3/testing/run.sh screenshots
```

`BUILD_ROOT` is a dedicated scratch directory outside the checkout. Build copies
the frontend there because KDE CMake installs git hooks; the host checkout's
hooks are not modified. Image creation copies the backend read-only into scratch.
The runner does not commit or push. The full frontend is built with testing on,
Debug configuration and `BUILD_OPENCONNECT=OFF` (the inherited test environment).
Package repositories are rolling; recorded image IDs and versions below identify
the actual tested environment rather than promising future identical packages.

## Final targeted regressions

The final pass adds regression-first coverage for delayed credential replies,
connection-property preservation through real reimport, appending/moving after
an unterminated final line (LF and CRLF), requesting system-owned profile secrets
when `profile-flags` is omitted, and refusing every blank effective remote,
including those inside connection blocks. Loaded-plugin tests cover missing and
explicitly empty secret replies with both locked and already loaded profiles.
The host secret-request predicate is tested directly; a live NetworkManager
GetSecrets round trip is not part of these component tests.

Final frontend result: **280 QTest passes, zero failures/skips; 8/8 OpenVPN 3
suites pass**. Full CTest retains the same **five unrelated baseline failures**.
Commands, red/green logs, caveats and refreshed screenshots are in the
[final targeted verification record](../../.hermes/results/openvpn3-final-targeted-summary.md).
The final delayed-reply-after-reimport correction and updated counts are recorded
in the [reimport race verification](../../.hermes/results/openvpn3-reimport-race.md).
Successful replacement now invalidates original connection secret replies until
a fresh configuration load; failed or cancelled imports retain useful replies.

## Earlier review regression coverage

| Finding | Behavioral coverage |
| --- | --- |
| SEC-1 availability, password fallback, KCM import | `openvpn3availabilitytest`: real isolated buses with no bus/provider, registered provider and activatable provider; child processes exercise the loaded plugin's actual import and flag maps. Widget tests cover blocked wallet saves and explicit system choice. Advertised providers here are synthetic, not real wallet stores. |
| SEC-2 normalization and transactional failure | Real backend imports; typed inline credentials, typed file references then deletion/reload, missing files, normalized credential edits, outgoing-tab handoff, explicit empty replacement password. |
| SEC-3 OTP | Editor output, blocked output, auth no-hints/password/empty-challenge replies and fresh OTP behavior. |
| SEC-4 public downgrade | Public choice absent for secret profiles and present only for existing legacy profiles. |
| BUG-1 scope | Nested remotes/auth, opaque nested certificates, scoped importer credentials, encrypted key/PKCS12 passphrase detection. |
| BUG-2 signals | QSignalSpy for source, table and block body edits plus credential/storage dirty state. |
| BUG-3 validity | Blank server rows, scoped remote/username requirements, normalization error status. |
| BUG-4 VPN properties | Timeout, persistent, user-name retained across save and real reimport. The widget's result contains only VPN properties. |
| BUG-5 unnecessary passphrase | Password-only and unencrypted-key prompts; encrypted PEM and PKCS12 prompts. |
| BUG-6 reimport | Current profile/password policy, failed imports, stored-credential warning predicate and actual modal cancellation for credential/storage-only edits. |
| BUG-7 line endings | Visit every tab and save untouched CRLF; edit CRLF without losing its convention. |
| BUG-8 row identity | Remove/move remotes with interspersed directives and trailing arguments; named-field edits retain other arguments/duplicates. |
| BUG-9 material | Real file embedding, reject non-UTF-8 binary PEM input, base64 PKCS12. |
| Auth requests | Missing/locked/corrupt/unsupported profile explanation; explicit hinted retries without profile, message/echo/focus semantics. |
| Host integration | Actual plugin discovery/loading, both OpenVPN plugins claiming `.ovpn`, no unknown-extension match, IPv6 eligibility and export refusal. KCM chooser wiring is code-reviewed; these component tests do not drive the whole KCM UI. |
| Directives user flows | Add/edit/remove/move ordered repeated directives, comments and multiline blocks; signal and selection/button behavior. |

The handoff's existing A–F implementation was retained and regression-tested.
New failures were recorded before the corresponding fixes in
[availability red](../../.hermes/results/codex-red-availability-directives.log),
[review red](../../.hermes/results/codex-red-review.log),
[save-gate red](../../.hermes/results/codex-red-final-gates.log),
[tab-transition red](../../.hermes/results/codex-red-tab.log) and
[empty-password red](../../.hermes/results/codex-red-empty-password.log).
Some intermediate red logs also contain test-harness failures (missing container
NSS identity and an invalid test widget lookup); the final run resolves those.

Earlier commands, results and screenshots are recorded in
[the previous frontend verification record](../../.hermes/results/openvpn3-review-summary.md).
Unit/component success is not proof of complete KCM save error propagation or a
combined Plasma/KWallet/VPN activation. Independent backend e2e evidence proves
real NetworkManager/openvpn3 activation, traffic, DNS, disconnect and reuse for
system-owned secret profiles, plus PEM, PKCS12 and legacy cases, after deletion
of original files. Those separate wallet/e2e workspaces were read-only and were
not modified or rerun by this frontend task.
