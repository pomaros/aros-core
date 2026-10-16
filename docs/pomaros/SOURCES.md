# Source and binary provenance

A product's `CORE.lock` identifies the exact common source commit and target ABI used for its next build. It is not a claim that every previously produced binary was built from that commit. Each binary package must retain its original source revision, product revision, toolchain identity, build configuration and checksums.

The initial public core is a curated source tree preserving official AROS ancestry. It excludes native-only peripheral drivers and private repository history. Guest boot manifests must exclude those peripheral modules as well; publishing this tree does not make earlier private guest binaries correspond to it. Rebuild and qualify new public-source guests before distributing them.

A corresponding-source release needs the applicable exact core revision plus product sources, build and installation scripts, source generators, dependency revisions and licences. Include the LLVM version and any compiler or runtime patches used. Hosted's Emu68 dependency and transformations are maintained in the Hosted product repository. External QEMU is a host prerequisite rather than part of the guest source tree.

A user must be able to obtain the required sources without access to private repositories. An unsigned or locally signed rebuild needs no Apple Developer ID certificate, notary credentials or private keys. Signed and notarized output has additional signatures, timestamps and tickets, so byte-identical signed DMGs are not a source-build guarantee.

The availability of this core alone does not assert that every historical binary's corresponding-source obligations have been satisfied. Release manifests and component-specific notices determine the required source inputs.
