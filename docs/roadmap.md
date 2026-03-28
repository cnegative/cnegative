# Roadmap

Near-term compiler work:

1. Generalize the runtime string story beyond `input()` so ownership is explicit for more than one producer.
2. Add module-level constants and finish visibility rules for future exported symbols beyond functions and structs.
3. Improve parser recovery so one syntax mistake does not collapse the rest of the file into follow-on errors.
4. Add more backend/runtime coverage for richer standard-library surface area.
5. Introduce optimization passes on typed IR before LLVM lowering.
