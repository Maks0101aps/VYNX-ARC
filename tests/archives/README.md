# Archive fixtures

RAR binaries are local, ignored test data. Before running tests in a new clone,
run `python scripts/fetch-rar-fixtures.py`. The downloader uses libarchive commit
`d294297f9ecade3b2446b677bd087ad84fb7965a` and checks decoded SHA-256 hashes
from `rar-fixtures.json`. Existing changed files are never overwritten.
CI restores these files before tests; packages do not contain them.

RAR fixtures are decoded from libarchive's upstream uuencoded test files at
https://github.com/libarchive/libarchive/tree/master/libarchive/test .
They contain ordinary test text/data and link metadata. They are test-only and
are never included in installers or portable packages. VYNX ARC does not include
RAR creation code. The libarchive fixtures are distributed under libarchive's
BSD license; see LICENSE.libarchive.

Expected stored RAR5 text: `hello libarchive test suite!` followed by a newline.
The basic RAR fixture includes a link and exercises conservative link rejection.
