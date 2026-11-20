/// Single compilation unit for acceptance test step definitions.
///
/// Cucumber-cpp uses __COUNTER__ to generate unique class names for each step.
/// When compiled as separate translation units, __COUNTER__ resets per file,
/// causing symbol collisions. Including all steps in one file avoids this.
///
/// Acceptance steps use the CommandClient abstraction and are separate from
/// the unit test steps (which call handlers directly).

#include "acceptance_steps.cpp"
