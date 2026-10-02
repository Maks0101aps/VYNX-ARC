# Archive fixtures

RAR fixtures are decoded from libarchive's upstream uuencoded test files at
https://github.com/libarchive/libarchive/tree/master/libarchive/test .
They contain ordinary test text/data and link metadata. They are test-only and
are never included in installers or portable packages. VYNX ARC does not include
RAR creation code. The libarchive fixtures are distributed under libarchive's
BSD license; see LICENSE.libarchive.

Expected stored RAR5 text: `hello libarchive test suite!` followed by a newline.
The basic RAR fixture includes a link and exercises conservative link rejection.
