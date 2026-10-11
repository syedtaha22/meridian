# Security

The engine is served over HTTP by the [server](backend/server). The server accepts JSON requests
from any origin. It validates each request and limits the depth, the time and the memory.

These problems are security problems:

- A request that exceeds the limits.
- A request that crashes the server.
- A request that reads or writes outside its own memory.

## Reporting

Report a security problem privately through the security advisories of the repository (open
`Security`, then `Report a vulnerability`). Do not open a public issue.

Include the request that triggers the problem and the engine version from `GET /health`.
