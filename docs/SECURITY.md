# Security policy

Version 0.1.0 is a development milestone. No stable version is currently supported
or certified production ready. Archive parsers process untrusted input; security
limits do not eliminate parser vulnerabilities or all filesystem races.

Report suspected vulnerabilities privately through GitHub private vulnerability
reporting for this repository when enabled. If unavailable, contact the maintainer
privately through their GitHub profile to arrange a secure reporting channel.
Do not publish an unpatched exploit or private archive in a public issue. Include
affected version, minimal non-sensitive reproduction, expected behavior and impact.

There is no automatic crash upload or telemetry. Passwords and archive contents
must never be submitted without explicit authorization. See docs/SECURITY_MODEL.md
for current controls and unresolved release gates.
