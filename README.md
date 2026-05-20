### Key Features
- **POSIX & C Compliance:** Developed strictly according to POSIX standards and C specifications.
- **Client-Server Architecture:** Uses a single-instance daemon (server) to handle orchestration, while subsequent executions act as clients to control the service.
- **IPC Messaging:** Client-server communication powered by Unix message queues.
- **Flexible Scheduling:** Supports task execution based on relative time, absolute time, or custom cyclic intervals (with command-line arguments support).
- **Interval Timers:** Core scheduling engine built around system interval timers.
- **Job Management:** Capabilities to list currently scheduled tasks and cancel active jobs on demand.
- **Clean Shutdown:** Ensures clean termination by automatically canceling all pending tasks upon exit.