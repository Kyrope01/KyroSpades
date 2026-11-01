# TweetNaCl (vendored)

Unmodified single-file Ed25519 implementation, used by `src/client_key.c` for
the Client Key extension.

* Upstream: <https://tweetnacl.cr.yp.to/> (authors: Daniel J. Bernstein,
  Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe, Sjaak Smetsers)
* Files taken from: <https://github.com/RIOT-OS/tweetnacl> (a verbatim mirror of
  the upstream release, same file names and contents)
* Files: `tweetnacl.c`, `tweetnacl.h`, `LICENSE`
* Licence: public domain, as stated by upstream
  ("TweetNaCl is a self-contained public-domain C library, so it can easily be
  integrated into applications."). The original notice is kept in `LICENSE`.

Nothing here is patched. The one thing the library expects from its host is
`void randombytes(unsigned char*, unsigned long long)`, which `src/client_key.c`
implements from the OS CSPRNG. Do not add local edits: a diff against upstream
stays empty on purpose so the file can be replaced wholesale.

`tests/client_key_test.py` pins the build to RFC 8032 test vectors 1 and 2, so a
replacement or a build-flag change that breaks the maths is caught immediately.
