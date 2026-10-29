# Dependencies and Licensing

## JUCE

CMake fetches JUCE 9.0.4 from the official JUCE repository and pins that tag for repeatable builds. JUCE is dual-licensed under AGPLv3 and a commercial JUCE license. Before distributing DAWg, choose a JUCE license and review its current terms against the intended distribution model.

## Project license

No license has been selected for DAWg itself. Add a project license only after choosing one. The project license does not replace JUCE's license requirements.

## C++ standard

DAWg requests C++26. JUCE's published minimum C++ standard is C++17, with C++20 required for the optional Windows MIDI Services configuration. C++26 compiler and standard-library support varies by toolchain; confirm it on each target platform.
