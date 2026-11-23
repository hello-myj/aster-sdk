# GitHub static distribution

The GitHub workflow builds TouchTest and the bootstrap packages. The full OBS catalog is selected by [publication.json](https://github.com/hello-myj/aster-releases/blob/main/publication.json). For an OBS release, use the [OBS publishing procedure](https://github.com/hello-myj/aster-releases/blob/main/docs/obs-distribution.md) with the bucket origin.

The default device channel is [Huawei OBS](https://aster-releases.obs.cn-south-1.myhuaweicloud.com/aster-source.json). The separate GitHub mirror needs no running Python server or device credentials. Its entry
is `https://github.com/hello-myj/aster-releases/releases/latest/download/aster-source.json`.
The descriptor contains a pinned immutable `base_url`. Setup profiles, catalogs,
manifest pages, icons and content-addressed blobs all belong to that release.
The device never calls the GitHub REST API. Existing local HTTP server URLs remain supported.

## Export locally

The APPS publisher prepares the two Store AOT architectures and stages profiles for the OSPTEK P4C5, lckfb and FAKE POD boards, verifies the AOT
packages, stages the existing setup/market schemas, then invokes the SDK exporter:

```sh
python ../aster-apps/tools/prepare-publication.py --sdk . --firmware ../aster-firmware \
  --repository hello-myj/aster-releases --tag public-example \
  --output build/publication/public-example
```

Add repeatable `--app ProjectDirectory` arguments to select other projects with
explicit `store.toml` hardware variants. Without them the initial catalog is TouchTest.
Program and independent resource files retain the normal Store installation format.
Source revisions and font/APP license notices accompany the assets.

The lower-level SDK command accepts previously published local input directories:

```sh
python tools/aster.py distribute export --setup-data SETUP_DATA --store-data STORE_DATA \
  --board osptek-p4-st7102 --board aster.esp32s3.lckfb --board fakepod-s31 \
  --repository hello-myj/aster-releases --tag public-example --output build/export-example
```

The output must not already exist. Metadata and payload checksums, bounded JSON,
hardware identity, resource paths and package integrity are checked before the
output is made visible. Identical font/resource blobs are shared across boards.
The GitHub exporter caps a release at 1,000 assets, each below 2 GiB; large
resource trees need fewer applications per release or a later bundled format.

## Publish

Use the `aster-releases` repository's **Publish static Aster distribution** Actions
workflow, or authenticate `gh` on the operator machine and run:

```sh
python tools/aster.py distribute publish --repository hello-myj/aster-releases \
  --tag public-example --output build/publication/public-example
```

The command creates a draft, uploads all files and only then makes it the latest
release. It never overwrites an existing tag. Failed uploads leave the previous
latest release active. Tokens are only consumed by `gh`, never embedded in files.
GitHub downloads require working HTTPS, DNS, redirects and Range requests; throughput
and reachability must be checked on the device's actual network.

## Device maintenance

The Web provisioning default comes from the firmware distribution configuration (currently OBS). A fresh
card still starts at the welcome screen and asks the user to connect Wi-Fi.
An already healthy card boots without contacting GitHub. Existing saved local
server settings are preserved.

To verify/install from a chosen source while preserving fonts, apps and saves,
stop all APPs, then run P4 `aster ui setup-source URL` or lckfb
`aster sys setup-source URL`. It uses saved Wi-Fi, verifies existing fonts, skips
matching files, installs the published Store and sets its source URL.
For a Store-only update, Store 0.3.4 and compatible firmware provide **Settings → Store update**. The Store uses the board's setup manifest and fetches only the Store package after confirmation. Devices running an earlier Store can install the new package over serial.

Static payload requests retain Range across origin redirects, while authentication
and cookies are stripped. Hosting ETags are opaque; the complete downloaded bytes
are still checked against the catalog's SHA256. HTTPS-to-HTTP downgrade is rejected.
The trust bundle includes ISRG Root X1, USERTrust ECC and GlobalSign Root CA - R3; peer/name/time validation
remain enabled. Never disable TLS validation to work around a download failure.

Cloud-specific upload tools live in `hello-myj/aster-releases`. The SDK only
exports the hosting-independent files with `--origin`; devices use the same
static protocol for GitHub and HTTPS object storage.
