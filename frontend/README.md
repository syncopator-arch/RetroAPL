# APL Terminal for Linux (Qt6)

A Qt6-based terminal emulator for APL that replicates the IBM 2741 APL terminal experience using the KAPL.ttf font.

## Features

- **Backspace-Overstrike Composition**: Press backspace once to move cursor back without deleting, then type another character to create composed APL symbols
- **KAPL.ttf Font Rendering**: Uses the classic APL font with anti-aliasing disabled for a "printed" appearance
- Two keyboard mapping options, "Classic" treats the keyboard as if it is laid out exactly as the 2741 was to the extent possible, and "Ascii" make a few alterations of that for convenience and familiarity.
- **Character Translation**: Full keyboard-to-APL character mapping with overstrike composition table

## Overstrike Examples

The terminal supports these overstrike combinations (and more):

- Circle + Bar → Rotate symbol
- Circle + Backslash → Transpose symbol  
- Circle + Minus → Circle-Minus (vertical flip)
- Quote + Quad → Quote-Quad
- Bar + Del → Sort symbols
- Caret + Tilde → Nand
- Many others (see `mappings.hpp` for complete list)

## Building on Ubuntu 22.04

### Prerequisites

```bash
sudo apt update
sudo apt install qt6-base-dev qt6-base-dev-tools cmake build-essential
```

### Build Steps

```bash
# Clone or extract the source code
cd APLTerminal

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build
make

# The executable will be in the build directory
./APLTerminal
```

### Font Installation

Make sure `kapl.ttf` is in one of these locations:
- Same directory as the executable
- `../fonts/kapl.ttf` (relative to executable)
- `./fonts/kapl.ttf` (in a fonts subdirectory)

## Project Structure

```
APLTerminal/
├── CMakeLists.txt           # Build configuration
├── main.cpp                 # Main application and window
├── APLTerminalWidget.h      # Terminal widget header
├── APLTerminalWidget.cpp    # Terminal widget implementation
├── mappings.hpp             # Character translation tables
├── kapl.ttf                 # APL font file
└── README.md               # This file
```

## How It Works

### Character Flow

1. **Input**: Keyboard ASCII → `OrigTrans[]` → KAPL.ttf font index
2. **Display**: Font index rendered on screen using KAPL.ttf
3. **Overstrike**: Font index + backspace + Font index → `OSTab[]` → Composed character
4. **Output to Interpreter**: Font index → `itrans[]` → Internal APL codes

### The Overstrike Algorithm

Based on the original Delphi implementation:

```cpp
if (backspace_pressed_once) {
    move_cursor_left();
    enter_overstrike_mode();
    // Don't delete the character!
}

if (character_typed_in_overstrike_mode) {
    composed = lookupOverstrikeTable(existing_char, new_char);
    replace_character_at_cursor(composed);
    exit_overstrike_mode();
}
```

This simple state machine replicates the IBM 2741 behavior where the typing element could strike the same position twice to create composite glyphs.

## Current Status

This is a **standalone terminal widget** that currently:
- ✅ Handles keyboard input with APL character translation
- ✅ Implements backspace-overstrike composition
- ✅ Renders using KAPL.ttf font
- ✅ Provides basic line editing

**Not yet implemented:**
- ❌ Integration with APL interpreter DLL
- ❌ Graphics window (eliminated, no longer needed)
- ❌ File editing mode
- ❌ Full screen management (scrolling, etc.)

## Next Steps

To integrate with your existing APL interpreter:

1. **Compile your C DLL for Linux**:
   ```bash
   gcc -shared -fPIC -o libapl.so *.c
   ```

2. **Create wrapper class** to load the DLL and handle callbacks

3. **Connect terminal signals** to interpreter:
   - `lineEntered()` → call `apl_start_exec()`
   - `interruptRequested()` → set interrupt flag
   - Callbacks → call terminal's `printOutput()`, `printInput()`

## Testing the Overstrike Feature

To test overstrike composition:

1. Run the terminal: `./APLTerminal`
2. Type a character (e.g., type `O` to get the Circle symbol)
3. Press Backspace **once** (cursor moves left, character stays)
4. Type another character (e.g., `|` for bar)
5. The two should combine into the Rotate symbol

Try these combinations:
- `O` + BS + `|` → Rotate
- `O` + BS + `\` → Transpose
- `O` + BS + `-` → Circle-Minus
- `'` + BS + `L` → Quote-Quad (where L gives Quad)

## Differences from Original Delphi Code

- Uses Qt6 `QTextEdit` instead of Delphi `RichEdit`
- Simpler cursor management (Qt's `QTextCursor` vs Windows `EM_*` messages)
- Font loading via `QFontDatabase` instead of `AddFontResource()`
- Signal/slot mechanism instead of callbacks (though callbacks can still be used)

## License

(Add your license here)

## Credits

Original Delphi/C implementation by Keith Doyle
Qt6 port based on analysis of the original Pascal source code
KAPL.ttf font from (original source unknown)

