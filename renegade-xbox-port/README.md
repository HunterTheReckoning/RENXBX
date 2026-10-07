# Renegade → original Xbox port: patches and compile test

This package holds the first round of porting work on EA's Command & Conquer: Renegade
source release, aimed at the original Xbox using the open-source nxdk toolchain.

So far it covers the engine's foundation libraries (`wwlib`, `WWMath`, `wwdebug`,
`wwsaveload`, `wwbitpack`, `wwtranslatedb`). In a test that imitated nxdk's compiler,
119 of their files compile, 14 PC-only files are deliberately left out, and 11 remain
(listed at the end). The next step is confirming that with nxdk's real compiler on your PC.

## What's inside

| Path | What it is |
|---|---|
| `patches/0001` – `0033` | The source changes, as a git patch series. Apply with `git am`. |
| `tools/nxdk_compile_test.sh` | Compiles the foundation libraries with nxdk's real compiler and writes a results file. |
| `tools/find_c_library_placeholders.py` | Lists functions in nxdk's C library that are halting placeholders, and which Renegade files call them. |
| `tools/fix_leaked_loop_vars.py` | Moves a loop variable's declaration above the loop when code uses it after the loop ends. |
| `tools/fix_two_word_casts.py` | Rewrites MSVC's two-word casts like `unsigned short(x)` as `(unsigned short)(x)`. |
| `tools/fix_extra_qualification.py` | Removes `Class::` from member declarations written inside their own class. |
| `tools/fix_shadowed_loop_vars.py` | After a loop variable's declaration was moved, makes later loops at the same level reuse it, as old MSVC's single variable did. |
| `tools/fix_for_scope.py` | Automatically fixes MSVC 6 loop-variable scoping errors found in compile logs. |
| `tools/fix_include_case.py` | Fixes `#include` filename case (already applied by patch 0003; kept for new code). |
| `tools/renegade_inventory.py`, `renegade_mapbudget.py` | The asset and memory-budget scripts from earlier. |
| `smoketest/` | A small Xbox program that runs the ported foundation on the console or xemu. |
| `renderbringup/` | An Xbox program that runs the 3D engine's own start-up and frame loop through the Direct3D layer. |
| `tools/extract_from_mix.py` | Lists or extracts files from a `.mix`/`.dat` archive by name, showing each texture's format. |
| `tools/xbox_sources.py` | The list of engine sources the Xbox build compiles (used by both Makefiles). |
| `tests/run_shim_tests.sh` | Unit tests: the port layer, the D3DX math, and the Direct3D layer (against stand-ins that record its GPU commands). |

## The patches

1. **vector.h template lookup.** MSVC 6 let templates use base-class members without naming
   them; standard compilers require it. Four lines in a header nearly everything includes.
2. **Portable replacements for risky inline assembly.**
   - The spinlock in `mutex.h` now uses an atomic builtin. The original changed a CPU register
     without telling the compiler.
   - The math helpers in `wwmath.h` use portable code on nxdk. `Float_To_Long` keeps the
     original round-to-nearest behaviour by using `lrint`, where a plain cast would truncate.
     `Inv_Sqrt` stored temporaries below the stack pointer, which is unsafe on the Xbox because
     games run in kernel mode and interrupts push onto the same stack.
3. **Include case.** About 3,160 `#include` lines corrected to match real file and folder names,
   because Linux, where nxdk runs, treats filenames as case-sensitive.
4. **`xbox_port.h` / `xbox_port.cpp`.** A small layer supplying what nxdk lacks:
   - `TCHAR` types and path-size limits
   - `_splitpath`, `_strlwr`, `_strupr`, `_wcsicmp`
   - DOS date/time conversions
   - text code-page conversions
   - `DebugBreak`
5. **C++ conformance fixes and wide-character printf.**
   - Fixes: loop scoping, `template<>` specializations, `typename`, two-word casts, and a dead
     allocator reference.
   - `_vsnwprintf` with Microsoft's rules (`%s` is a wide string, `%S` a narrow one), used for
     on-screen text.
6. **Further fixes.**
   - A leaked loop variable in `BitPacker.cpp`.
   - Threads run without Windows structured exception handling.
   - An `_int64` alias.
   - Explicit specializations written with `{}` so they are definitions; without it they
     compile but fail at link time.
   - Blitter specializations.

7. **`Code/xbox/include/`.** Stand-ins for old Microsoft headers nxdk doesn't have
   (`new.h`, `memory.h`, `mmsystem.h`, `timeapi.h`). This folder goes on the include path
   for Xbox builds only, so it can never shadow the real headers in a PC build.

8. **Wide characters, settings, threads, and fixes found by the real compiler.**
   - `Code/xbox/include/xbox_prelude.h`, force-included into every file, makes the engine's
     `WCHAR` the native `wchar_t` (as modern Windows SDKs do). nxdk's own headers keep their
     `unsigned short` type under another name, so nxdk itself is untouched.
   - `RegistryClass` (game settings, used in 45 files) rebuilt on a small settings file,
     written crash-safely (temporary file, then rename, with recovery if power is cut mid-save).
   - `_beginthread`, `GetComputerName`, `GetUserName`, `FormatMessage` and `getpid` stand-ins.
   - Threads are never killed on Xbox (the kernel can't do it safely); `Stop` waits for them
     to exit on their own, as the engine's threads are written to do.
   - Strictness fixes: `this->`, extra qualifications, a missing `<ctype.h>`, wide-string
     types in `ini`, and network strings pinned to 16 bits per character in `bitstream`.

9. **Strings through `printf`-style calls, and Xbox system info.**
   - Nine places passed a `StringClass` object straight into a `printf`-style function. That
     only worked under MSVC by accident (the object holds nothing but a pointer to its text);
     Clang rejects it because the call would abort at runtime. Each now passes the text with
     an explicit `(const char *)`. More of these will turn up in the game code.
   - `cpudetect.cpp` reads memory figures from the kernel's `MmQueryStatistics` and reports
     the Xbox kernel version instead of a Windows version.

10. **`cpudetect.cpp` finished.** Windows platform IDs defined, the system log says "Xbox"
    rather than matching a Windows version, two more strings passed to `printf` as text,
    and one extra qualification removed.

11. **`wwlib/except_xbox.cpp`.** The PC crash handler (`Except.cpp`) is left out of the Xbox
    build, but the game calls its interface from about a dozen places. This provides it:
    thread registration is kept (so `Get_Main_Thread_ID` works), while stack walks, symbol
    lookups and crash dumps report that nothing is available.

12. **Start-up diagnostics.** `PORT_TRACE` markers (off unless a test program switches them
    on) before each step of `cpudetect`'s start-up code, which runs before `main()`. Also
    removes a leftover test event that `thread.cpp` created at start-up.

13. **Clock speed on the Xbox.** The smoke test showed start-up hanging in `cpudetect`'s
    clock-speed measurement, a timing loop that reads the CPU's cycle counter with RDTSC
    written as raw bytes. On Xbox the speed now comes from `QueryPerformanceFrequency`
    (nxdk derives it from the hardware: 733.33 MHz on a stock console), which also saves
    1.2 s at every start-up. `wwprofile` reads the counter with the compiler's built-in.

14. **Rounding without the C library.** nxdk's `lrintf` exists only as a placeholder that
    halts the program (`assert(0)`), which the smoke test hit. `Float_To_Long` now uses
    the original's x87 `fistp` instruction directly, written so the compiler understands
    it. It matches `lrint` exactly, including ties to even.

15. **Saving settings to the Xbox disk for real.** The Xbox kernel caches disk writes and
    writes them out later, so a reset soon after saving lost the settings file. After
    saving, the store now asks the kernel to write the file and its folder to disk, and
    reports whether that worked (the smoke test shows it as `disk flush`).

16. **The 3D engine (`ww3d2`), stage 1: the 49 files that don't use Direct3D** (animation,
    skeletons, collision, LOD, particles, mesh building). Mostly the same fixes as the
    foundation. One EA bug surfaced: two lighting cutoffs in `lightenvironment.cpp` had no
    type, which old MSVC treated as `int`, so the shipped game has always used 0 rather than
    the intended 0.5. The type is now explicit and the shipped value is kept. The port layer
    gains Windows' `lstr*` string functions and `GetCurrentDirectory` (reports `D:\`).

17. **The 3D engine, stage 2, milestone 1: Direct3D 8 headers.** `Code/xbox/include` gains
    `d3d8.h`, `d3d8types.h`, `d3d8caps.h` and `d3dx8.h`, covering exactly the roughly 400
    Direct3D names Renegade uses, with the DirectX 8 SDK's real values (the engine indexes
    arrays by render-state and format numbers, and decodes vertex formats bit by bit). The
    D3DX math (matrix product, transpose, vector transform, vertex sizes) is implemented and
    tested. Two color functions in `dx8wrapper.h` that juggled the stack in inline assembly
    get exact portable versions. PC-only window handling, desktop gamma, a dialog box and
    movie capture are skipped on Xbox. With this, 50 of the 51 Direct3D files compile; the
    last draws fonts with Windows GDI and waits for its replacement.

18. **The Direct3D layer, step 1: device bring-up** (`Code/xbox/d3d8/`, on nxdk's pbkit).
    Direct3D object and device creation (640x480, 32-bit color, 24-bit depth with stencil),
    `Clear` sent as the NV2A's own clear command, `Present`, viewports and stored render state,
    textures, surfaces and vertex/index buffers in GPU-visible memory, and the D3DX texture
    helpers (format conversion, box-filtered mipmaps). Drawing comes in step 2. The layer
    reports an unidentified GPU vendor on purpose: the engine's NVIDIA workaround would turn
    off DXT1, which most of Renegade's textures use and the NV2A decodes natively. In the
    engine, `DX8Wrapper` calls `Direct3DCreate8` directly (no DLLs on Xbox), the animation-sound
    manager gets a placeholder until audio is ported, and the font code compiles with
    placeholder glyphs until the SDL_ttf replacement.

19. **Start-up diagnostics in the Direct3D layer.** Progress markers through device creation
    and the first frames (shown only when a test program switches them on). The text screen
    now stays visible until the first frame is presented, and the layer no longer switches
    the video mode a second time, which wiped the text.

20. **Keep start-up text visible after `pb_init`,** which ends by showing its own blank frame buffer.

21. **Start-up markers through the engine's own device set-up** (missing texture, texture loader thread, D3DX mipmaps).

22. **File names resolve against the game's folder (`D:\`), as Windows resolves them against the current folder.** nxdk resolves names in the kernel's drive-letter directory, so the engine's relative names found nothing, and `..\name` hung start-up.

23. **Formatted start-up markers** through `ShatterSystem::Init` and the asset manager's load-on-demand path.

24. **The Direct3D layer, step 2: drawing.** A vertex program (`xbox_ffp.vs.cg`, 50
    instructions) reproduces Direct3D 8's fixed-function transform and directional lighting,
    which is what Renegade's light environments send. Its microcode (`xbox_ffp_vs.inl`) was
    generated with nxdk's Cg compiler and is committed with it, together with the constant
    layout the compiler reported. Draws read the Direct3D vertex buffers directly; depth,
    alpha test, blending and color mask follow render state. Textures come in step 3.

25. **Live-object counters and culling in the Direct3D layer.** `XboxD3D_Get_Stats` reports
    live textures, buffers and surfaces for leak checks; `D3DRS_CULLMODE` now culls.

26. **Diagnostic switch in the Direct3D layer** to output w = 1 (no perspective correction),
    for locating the depth errors seen with crossing planes.

27. **Z-buffer instead of pbkit's w-buffer.** pbkit turns on the NV2A's w-buffer every frame,
    which interpolates depth wrongly per triangle (surfaces met along triangle edges). The
    layer now sets the depth mode from Direct3D's render state on every draw.

28. **The Direct3D layer, step 3: textures.** Each stage's texture is bound with the NV2A's
    format, addressing and filtering; DXT textures are used as stored, uncompressed ones are
    swizzled before their next draw. Mip levels are packed as the NV2A expects. Direct3D's
    texture-stage operations are translated into register combiners
    (`xbox_d3d8_combiners.cpp`, unit-tested). Not yet: non-power-of-two textures, texture
    coordinate generation and texture matrices.

29. **Culling mapping corrected.** The NV2A judges winding as it appears on screen, so
    Direct3D's "cull clockwise" is the NV2A's "front face counterclockwise". Patch 25 had it
    reversed, hidden by an equally reversed test cube; Westwood's own Minigunner showed it.

30. **Texture coordinate sets per stage, and on-screen notices.** Each texture stage reads the
    coordinate set its material chooses (`D3DTSS_TEXCOORDINDEX`), as Renegade's lightmapped
    surfaces need. The layer also lists what it met but doesn't support yet; the test program
    shows the list in its model scenes.

31. **`wwphys` compiles:** physics, collision, visibility and pathfinding (93 files). Umbra, a
    third-party visibility library, is switched off in Westwood's source, so nothing is missing.
    Loop variables now keep old MSVC's semantics everywhere: where a declaration was moved above
    its loop, later loops at the same level reuse it rather than hiding it (11 loops, one of
    them in `dx8wrapper.cpp`).

32. **Level loading skips objects of classes not yet ported** instead of adding a NULL object
    (which stopped the tutorial level's load), and reports each missing class once by its chunk
    ID. Nothing changes once every library is present.

33. **Texture matrices, generated coordinates and coordinate sets 0–3.** Renegade's texture
    mappers scroll, scale, rotate and tile textures through each stage's texture matrix, and its
    environment and screen mappers generate coordinates from the camera-space normal, position
    or reflection vector. The layer ignored these, which left dark blocky patches on surfaces
    that use them. The vertex program now follows Direct3D 8's fixed-function rules for both
    texture stages.

All thirty-three were verified to apply cleanly to EA's repository.

**Every Xbox build of the engine needs three extra compiler flags:** `-I Code/xbox/include`
for the stand-in headers, `-include Code/xbox/include/xbox_prelude.h` (see patch 8), and
`-Wno-c++11-narrowing`. MSVC accepted constant tables like
the seed values in `random.cpp`, wrapping them to negative numbers; this flag keeps that
behaviour. The compile test script adds both.

---

## The 3D engine (`ww3d2`): plan

A survey of the source shaped the plan:

- **Half of `ww3d2` (54 of 105 files) never touches Direct3D.** Patch 16 ports those.
- **Direct3D is almost entirely contained in `ww3d2`.** Outside it, only four files use it,
  lightly: Bink video (being replaced anyway), a depth-bias setting for weather, a wireframe
  debug view, and render targets for projected shadows.
- **Inside `ww3d2`, it goes mostly through one class, `DX8Wrapper`**, which is the natural
  seam for the replacement.
- **Renegade uses a small, fixed-function part of Direct3D 8:** about 50 interface methods
  and 8 D3DX helpers. No programmable shaders and no user-pointer drawing.

The Xbox's GPU, the NV2A, was designed as Direct3D 8 hardware. So stage 2 is a **small
Direct3D 8-compatible layer on nxdk's pbkit**, covering only what Renegade calls, so
`dx8wrapper.cpp` and the other Direct3D files compile largely unchanged and only that layer
talks to the GPU.

**Milestone 1 is done (patch 17):** the headers exist and the Direct3D code compiles against
them. **Milestone 2** implements the layer on pbkit so it actually draws, plus a replacement for
the one file that renders fonts with Windows GDI (using the game's own TrueType fonts through
nxdk's SDL_ttf). **Milestone 3** is a render smoke test: a real W3D model loaded from a game
archive and drawn on the Xbox.

In the compile test, the three `ww3d2` files still waiting on a replacement are listed as
`LATER`: two sound files (audio) and `render2dsentence.cpp` (fonts).

## Render bring-up: the 3D engine starting on the Xbox

`renderbringup/` runs the engine's real start-up and frame loop, through the port's Direct3D
layer: `WW3D::Init` (creates Direct3D, enumerates devices, starts the 3D subsystems), then
`WW3D::Set_Render_Device` (chooses formats, creates the device, builds the missing-texture
placeholder), then frames through `WW3D::Begin_Render` / `End_Render`, cycling the clear color.

```
eval "$(/workspaces/nxdk/bin/activate -s)"
cd /workspaces/renegade-xbox-port/renderbringup
make CODE=/workspaces/RENXBX/Code
cp renegade_render.iso /workspaces/RENXBX/
```

The first build compiles all 235 engine files and takes a while. In xemu, load the ISO and
reset. You should see status lines, including `d3d:` markers from the Direct3D layer, then a
spinning cube with a different color at each corner, drawn through the engine's `DX8Wrapper`,
over a background whose color changes smoothly. If something stops before the first frame, the
last line on screen shows where.

On screen: free memory (with its change since a baseline taken at frame 300, and the lowest
seen) and the Direct3D layer's live objects. In an unchanged scene, free memory should stay
flat; a steady fall is a leak. Controller:

| Button | Does |
|---|---|
| A | Churn mode: the cube's buffers and a 64x64 texture are created and released every frame, so memory must still stay flat |
| B | Culling on/off. With culling on, only the cube's outside should ever show |
| (boot) | **Test menu.** D-pad up/down to choose, A to start. Each test relaunches the program (`XLaunchXBEEx`) and loads only its own assets, so it starts on a freshly started machine. Tests: Render (cube, depth planes, textures), Models (Minigunner, tutorial terrain), Level (M00 through `wwphys`), Animation (Havoc) |
| Start + Back | Hold both: back to the test menu |
| Y | Next scene within the current test |
| D-pad left/right, up | Animation test: previous/next animation, blending on/off. Plays Renegade's human movement set by the game's own names (`S_A_HUMAN.H_A_A0A0` stand, `A0A1`–`A4` run, `A0B1`–`B4` walk, crouch, jump, climb, dive), each loaded on demand; missing names are shown as not found. Switching blends from the old animation to the new over a quarter second |
| X | Reset the memory baseline |
| D-pad left | In the level scene: visibility culling on/off (the engine's precomputed visibility only draws what can be seen from the camera's vis sector) |
| Black | Cycle the on-screen text: full, compact (memory and Direct3D rows only), off. Counters keep running |
| White | In the terrain scene: show or hide the GDI building interiors and doors from `M00_Tutorial.mix`, which are added exactly as stored (no positioning), to see whether they already sit in the level's coordinates |

The two real textures come from your game files. Extract them into `renderbringup/data/`
(which becomes the root of the disc) before building:

```
python3 /workspaces/renegade-xbox-port/tools/extract_from_mix.py /workspaces/gamedata/always.dat \
    /workspaces/renegade-xbox-port/renderbringup/data l05_grass.dds if_gdi_logo2.dds
ln -sf /workspaces/gamedata/always.dat /workspaces/gamedata/Always2.dat /workspaces/gamedata/always.dbs \
    /workspaces/renegade-xbox-port/smoketest/data/M00_Tutorial.mix \
    /workspaces/renegade-xbox-port/renderbringup/data/
```

The program reads `Always2.dat` and `always.dat` through the engine's archive classes, in the
game's order (loose files, then `Always2.dat`, then `always.dat`, then the level archive), so the model's meshes,
skeleton and textures load by name exactly as in the game.
| Start | Diagnostic: w = 1, no perspective correction (for locating depth problems) |
| Sticks, triggers, Back | In the Minigunner scene: left stick flies and strafes, right stick looks around, right/left trigger rise/descend, Back resets the view. The model stops turning while you fly |

## Smoke test: running the foundation on the Xbox

`smoketest/` builds a small Xbox program that links the ported foundation libraries and
checks they actually work on the console (or xemu), printing PASS/FAIL lines on screen:

| Test | What it checks |
|---|---|
| string, widestring | `StringClass` formatting; wide `printf` with Microsoft's `%s` rules |
| rounding, inv_sqrt | `Float_To_Long` rounds like the original x87 code; portable `Inv_Sqrt` |
| cpu, memory | `cpudetect`'s CPUID assembly, and memory from the Xbox kernel |
| clock | The engine timer and CPU cycle counter both advance (reruns the old start-up measurement safely) |
| threads | `_beginthread` replacement, timers, and the Xbox-safe `Stop()` |
| settings, disk flush | `RegistryClass` saving to and reloading from the hard disk (`E:`), and the kernel confirming it reached the disk |
| one line per archive | every file inside each `.mix`/`.dat` on the disc read in full, with a CRC |

The settings test counts runs: reset xemu and run it again, and the number should go up,
which proves the value really was saved to the hard disk.

### Build it (in the codespace)

1. Upload a small game archive. Drag `always3.dat` (from your game's `Data` folder) into the
   codespace Explorer, then move it into the test's `data` folder:
   ```
   mv /workspaces/RENXBX/always3.dat /workspaces/renegade-xbox-port/smoketest/data/
   ```
   Any `.mix` or `.dat` works; small ones build and run fastest.
2. Build:
   ```
   eval "$(/workspaces/nxdk/bin/activate -s)"
   cd /workspaces/renegade-xbox-port/smoketest
   make CODE=/workspaces/RENXBX/Code
   ```
   The first build compiles all 131 foundation files and takes a few minutes. It ends with
   `renegade_smoke.iso`. If it stops with errors instead (most likely "undefined symbol"
   errors from the link step), copy the last 30 or so lines into the chat.
3. Copy the ISO where you can download it, then right-click it in the Explorer → Download:
   ```
   cp renegade_smoke.iso /workspaces/RENXBX/
   ```

### Run it in xemu

Choose **Machine → Load Disc**, pick `renegade_smoke.iso`, then **Machine → Reset**. The
screen first shows `start-up:` lines as the program starts (these run before `main()`),
then the results within a few seconds (longer for big archives). If the program stops
early, the last `start-up:` line shows where. Take a screenshot
and share it. Reset again to check the settings run count goes up.

`make clean` removes the build files, including the object files it puts next to the
engine sources.

## Setup with GitHub Codespaces (recommended: nothing installed on your PC)

1. Fork `github.com/electronicarts/CnC_Renegade` to your account, open your fork, and choose
   **Code → Codespaces → Create codespace on main**. Paths below assume the fork is named
   `RENXBX`; use your fork's name.
2. In the codespace terminal:
   ```
   sudo apt-get update && sudo apt-get install -y clang lld llvm bison flex cmake unzip
   cd /workspaces && git clone --recursive https://github.com/XboxDev/nxdk.git
   eval "$(/workspaces/nxdk/bin/activate -s)"
   cd /workspaces/nxdk/samples/hello++ && make
   ```
3. Drag this package's zip into the Explorer panel, then:
   ```
   cd /workspaces && mv RENXBX/renegade-xbox-port.zip . && unzip renegade-xbox-port.zip
   cd RENXBX && git am /workspaces/renegade-xbox-port/patches/*.patch
   cd /workspaces/renegade-xbox-port
   ./tests/run_shim_tests.sh /workspaces/RENXBX/Code
   ./tools/nxdk_compile_test.sh /workspaces/RENXBX/Code
   cp results-default.txt /workspaces/RENXBX/
   ```
4. Right-click `results-default.txt` in the Explorer → **Download**.

Stop the codespace from `github.com/codespaces` when you're done to save your free hours.
In each new terminal, run the `eval` line again before building.

## Alternative: setup with WSL on your own PC (needs hardware virtualization)

nxdk runs on Linux. On Windows, the easiest way is WSL (Windows Subsystem for Linux),
which gives you a real Ubuntu terminal inside Windows.

### 1. Install WSL with Ubuntu

Open **PowerShell as Administrator** (right-click the Start button → Terminal (Admin)) and run:

```
wsl --install -d Ubuntu-24.04
```

Restart when asked. Ubuntu then opens and asks you to create a username and password
(they're separate from your Windows login). From now on, "the Ubuntu terminal" means
that window. You can reopen it from the Start menu by searching for **Ubuntu**.

### 2. Install the build tools

In the Ubuntu terminal:

```
sudo apt update
sudo apt install -y build-essential git cmake bison flex clang lld llvm python3 unzip
clang --version
```

Ubuntu 24.04 installs Clang 18, which is what you want. nxdk warns that **Clang 19.0
through 20.1.2** have an optimization bug that can break compiled code, so avoid those.

Tell git who you are (needed to apply the patches):

```
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

### 3. Get nxdk and prove it works

```
git clone --recursive https://github.com/XboxDev/nxdk.git ~/nxdk
eval "$(~/nxdk/bin/activate -s)"
cd ~/nxdk/samples/hello && make
cd ~/nxdk/samples/hello++ && make
```

The `--recursive` part matters: it fetches the C library, C++ library, and other pieces
that a GitHub "Download ZIP" leaves out. The first `make` builds nxdk's own libraries and
takes a while; the C++ sample builds the C++ library too. Success means each sample
folder ends up with a `bin/default.xbe` (and usually an `.iso`) with no errors.
If you have xemu set up, you can boot one to see "Hello" on screen.

The `eval` line activates nxdk for the current terminal only. Run it again whenever you
open a new Ubuntu terminal.

### 4. Get Renegade's source and apply the patches

Clone EA's repository. Use `git clone` rather than downloading the ZIP: the repository has
ignore rules that would leave some files untracked in a ZIP-based copy, and patch 3
would then fail.

```
git clone https://github.com/electronicarts/CnC_Renegade.git ~/CnC_Renegade
```

Copy this package into Ubuntu. Your Windows drives appear under `/mnt/c`; for example,
if the ZIP is in your Windows Downloads folder:

```
cd ~
unzip /mnt/c/Users/YOUR_WINDOWS_NAME/Downloads/renegade-xbox-port.zip
```

Apply the patches:

```
cd ~/CnC_Renegade
git am ~/renegade-xbox-port/patches/*.patch
git log --oneline | head -7
```

You should see the thirty-three port commits on top of EA's history.

Keep both folders inside your Ubuntu home (`~`), not under `/mnt/c`. Builds there are much
faster, and filename case behaves the way nxdk expects.

### 5. Run the tests

```
cd ~/renegade-xbox-port
./tests/run_shim_tests.sh ~/CnC_Renegade/Code
eval "$(~/nxdk/bin/activate -s)"
./tools/nxdk_compile_test.sh ~/CnC_Renegade/Code
```

The unit tests should print six "passed" lines. The compile test shows a live count and writes:

- `results-default.txt`: upload this.
- `logs-default/`: the full compiler output for every file.
- `obj-default/`: the compiled object files.

To copy the results to your Windows desktop:

```
cp results-*.txt /mnt/c/Users/YOUR_WINDOWS_NAME/Desktop/
```

---

## What the results should show

**The wide-character question (settled).** The engine mixes `WCHAR`, `wchar_t` and
`unsigned short` for wide strings, because under MSVC 6 they were the same type. Clang's
`-fno-wchar` was tested and ruled out: nxdk's C library needs a real `wchar_t`. Patch 8's
prelude solves it instead by making the engine's `WCHAR` the native `wchar_t`.

**Expected results.** With patches 1–9, nxdk's real compiler built 130 of 131 files. The
last one, `cpudetect.cpp`, is fixed by patch 10, so all 131 should now compile. Anything
that fails is new information; upload `results-default.txt` either way.

**Settings file location.** Game settings are saved to `D:\renegade_settings.dat`. On the
Xbox, `D:` is the folder the game was launched from, which is writable when it runs from the
hard drive. Running from a disc would need a different location (for example the standard
`E:\UDATA` save area); that's a decision for later.

**Left out on purpose** (PC-only or unused by the game): DirectDraw (`ddraw`, `dsurface`,
`convert`), the web launcher, the COM helpers, the Windows keyboard
handler and message loop, version checking, the crash handler, Windows resources,
regular expressions (whose `gnu_regex.c` is missing from EA's release), SHA hashing, `mpu.cpp`, and `xsurface.cpp` (a legacy
software blitter whose assembly jumps between separate blocks, which Clang doesn't support). If the linker later finds the game needs any of these, they get small Xbox
versions.

## Troubleshooting

- **`nxdk is not active`**: run `eval "$(~/nxdk/bin/activate -s)"` in this terminal.
- **`git am` fails on patch 3**: the source was probably unzipped rather than cloned. Clone it
  as in step 4.
- **`git am` says "Committer identity unknown"**: run the two `git config` lines from step 2.
- **A sample fails to build**: check `clang --version` (avoid 19.0–20.1.2) and that nxdk was
  cloned with `--recursive`. If not, run `git submodule update --init --recursive` in `~/nxdk`.
- **Anything else**: paste the error text into the chat.
