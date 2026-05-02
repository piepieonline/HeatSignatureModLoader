## Installation:
* Clone the repo
* Rename `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and change variables in angle brackets.
* Run `vcpkg install`
* Run `cmake --preset x86-debug`
* Run `cmake --build --preset local-debug`
* Run the game. A terminal window should open and contain logs regarding hook installation.