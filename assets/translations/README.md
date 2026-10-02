# Translations

English is the source language. Ukrainian and Russian catalogs use Qt Linguist
TS files. Update with `lupdate apps/gui -ts assets/translations/vynx_uk.ts
assets/translations/vynx_ru.ts` and build with lrelease through CMake.
Backend diagnostics remain English until error-code localization is implemented.
