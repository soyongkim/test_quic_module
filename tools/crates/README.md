This directory contains tools for importing third-party Rust crates and
constructing BUILD.gn files from them.

# Depending on third-party crates

The `//third_party/rust/third_party.toml` crate defines the set of crates
depended on from first-party code. Any transitive dependencies will be found
from those listed there. The file is a subset of a
[standard `Cargo.toml` file](https://doc.rust-lang.org/cargo/reference/manifest.html),
but only listing the `[dependencies]` section.

To use a third-party crate "bar" version 3 from first party code, add the
following to `//third_party/rust/third_party.toml` in `[dependencies]`:
```toml
[dependencies]
bar = "3"
```

To enable a feature "spaceships" in the crate, change the entry in
`//third_party/rust/third_party.toml` to include the feature:
```toml
[dependencies]
bar = { version = "3", features = [ "spaceships" ] }
```

# Generating `BUILD.gn` files for third-party crates

To generate `BUILD.gn` files for all third-party crates, and find missing
transitive dependencies to download:
1. Change directory to the root src/ dir of Chromium.
1. `tools/crates/crates.py gen`

This will generate a `BUILD.gn` file for each third-party crate, which should be
added to the repository with `git add`. The full list of new `BUILD.gn` files
would be visible in `git status`.

# Downloading missing third-party crates

To download crate "foo", at the latest release of major version 4.x:
1. Change directory to the root src/ dir of Chromium.
1. `tools/crates/crates.py download foo 4`

This will download the crate and unpack it into
`//third_party/rust/foo/v4/crate`. The entire `v4` directory, which includes the
`crate` subdirectory as well as a generated `README.chromium` file, should be
added to the repository with `git add third_party/rust/foo/v4`.

Once all the crates are downloaded and `crates.py gen` completes, a CL can be
uploaded to go through third-party review.

# Patching third-party crates.

You may patch a crate in tree, but save any changes made into a diff file in
a `patches/` directory for the crate. The diff file should be generated by
`git-format-patch` each new patch numbered consecutively so that they can be
applied in order. For example, these files might exist if the "foo" crate was
patched with a couple of changes:

```
//third_party/rust/foo/v4/patches/0001-Edit-the-Cargo-toml.diff
//third_party/rust/foo/v4/patches/0002-Other-changes.diff
```

# Updating existing third-party crates

To update a crate "foo" to the latest version you must just re-import it at this
time. To update from version "1.2" to "1.3":
1. Remove the `//third_party/rust/foo/v1/crate` directory, which contains the
upstream code.
1. Re-download the crate with `tools/crates/crates.py download foo 1`. This will
find the latest matching version on https://crates.io. If a more specific
version is desired, you may specify the full version.
1. If there are any, re-apply local patches with
`for i in $(find third_party/rust/foo/v1/patches/*); do patch -p1 < $i; done`
1. Run `tools/crates/crates.py gen` to re-generate all third-party `BUILD.gn`
files.

# Directory structure for third-party crates

The directory structure for a crate "foo" version 3.4.2 is:
```
//third_party/
    rust/
        foo/
            v3/
                BUILD.gn  (generated by crates.py)
                README.chromium
                crate/
                    Cargo.toml
                    src/
                    ...etc...
                patches/
                    0001-Edit-the-Cargo-toml.diff
                    0002-Other-changes.diff
```