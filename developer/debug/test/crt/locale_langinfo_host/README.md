# Locale information regression

Run the actual POSIX locale-information entry points and immutable tables:

```sh
python3 -B developer/debug/test/crt/locale_langinfo_host/run.py --output /absolute/new-output
```

Four concurrent readers use explicit C/C.UTF-8 locale handles while another
thread alternates the modeled current locale. Explicit queries must never read
or change the current locale. Check date/time format strings, calendar names,
codesets, radix and thousands separators, invalid items and locale handles.
The actual stdc `setlocale` wrapper executes against modeled internal locale
storage. Query and update counters detect a null query resetting C.UTF-8.
OS locale storage and thread scheduling are modeled; these are HOST ASan/UBSan checks, not a guest
or broad locale-conformance result. The supported locale names follow stdc's
C and C.UTF-8 definitions. No new locale implementation is introduced here.

Compiler/run diagnostics and exact source identities are retained in the output.
Generated test executables are temporary and removed after the run. Three exact
negative mutations must fail at runtime: reading the global locale for an
explicit handle, resetting the locale during a null query, and returning a
calculated date in place of its format.
