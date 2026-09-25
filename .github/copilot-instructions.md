# Agent Capabilities & Constraints

## CRITICAL RESTRICTIONS
- NEVER execute build commands (`npm run build`, `make`, `cmake`, `cargo build`, `platformio run`, `idf.py build`, `tools/build_idf60.ps1`, etc.) unless explicitly commanded by the user in the prompt.
- This restriction applies to ALL ways of triggering a build: raw terminal commands, the VS Code task runner (e.g. "ESP-IDF: Build via project wrapper"), CMake/ESP-IDF tool integrations, and any other build/compile tool — not just literal shell invocations.
- NEVER run flash, upload, or deployment tools unless explicitly asked.
- "Explicitly commanded" means the user's current message contains a direct request to build/flash/deploy (e.g. "build it", "run the build task"). Do NOT infer permission from context like "let's test this", "does this compile", or a previous build earlier in the conversation.
- If you believe a build is necessary to answer a question, ASK for permission first and wait for the user's reply before running anything.

