# Password copies and lifetime audit

Passwords are not logged, put into command lines, persisted in settings or sent
through Explorer IPC. The current archive may retain its password until close,
successful replacement by another archive, or application destruction. An active
worker holds a reference until it stops; cancellation must not erase bytes while
a decoder still borrows them.

| Boundary | Ownership and remaining limitations |
| --- | --- |
| QLineEdit / QInputDialog | Qt owns its text and may make implicit/shared UTF-16 copies. Fields used for create/extract/modify are cleared after transfer. Clearing is not a guarantee that every Qt allocation is wiped. |
| QString to UTF-8 | A short-lived QByteArray is copied into SecretUtf8 and filled with zeros. Other Qt/input-method/allocator copies are outside this guarantee. |
| C++ job callbacks | One non-copyable SecretUtf8 owns the bytes. shared_ptr copies move through std::function/QtConcurrent, rather than copying std::string secrets. Its destructor uses volatile writes over owned live bytes. Completed job captures are explicitly released. |
| Retained open archive | MainWindow holds the same shared secret. Close/reset releases this ownership. Modification/reopen shares it rather than retaining another QString password in a timer callback. |
| CXX | Calls borrow a rust::Str view during a synchronous worker call; the owning C++ secret remains alive. CXX does not transport passwords back to the GUI. |
| Rust CreateOptions | A Zeroizing<String> owns the creation password, including FFI-created options. Other operations borrow &str. |
| UnRAR adapter | Its owned UTF-16 password vector is Zeroizing and lives with the native handle. UnRAR's own native copies/key state require separate backend review. |
| sevenz-rust2 / ZIP crypto | Backend password conversions, cloned vectors, cipher schedules and codec allocations are not all guaranteed to wipe. They are ephemeral but are an unresolved backend zeroization gate. |

Volatile writes provide a meaningful best-effort wipe of the application's owned
live bytes. They do not erase earlier reallocations/copies, freed allocator pages,
swap, crash dumps, registers, framework buffers or all cryptographic backend state.
The application does not claim guaranteed secure memory erasure. A backend-wide
key/password audit remains required before a production security claim.
