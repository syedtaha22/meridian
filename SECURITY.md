# Security

The engine is a program that reads UCI commands from standard input and runs searches. It
validates each command and limits the hash size, the threads and the depth.

These problems are security problems:

- A command or an option value that exceeds the limits.
- A command that crashes the program.
- A command that reads or writes outside its own memory.

## Reporting

Report a security problem privately through the security advisories of the repository (open
`Security`, then `Report a vulnerability`). Do not open a public issue.

Include the commands that trigger the problem and the engine version from the `id name` line.
