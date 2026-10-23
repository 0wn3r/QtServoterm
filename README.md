# Servoterm

Qt version of stmbl Servoterm. It is still a work in progress, stay tuned!

## Dependencies

* Qt6 (widgets, serialport, and network modules)

The relevant development packages to install on Debian/Ubuntu are (package
names vary somewhat by release, so check `apt search qt6` if these aren't
found):

* qt6-base-dev
* qt6-serialport-dev (or `libqt6serialport6-dev` on some releases)

## Compiling

### qmake

On Linux, run the following commands in the top-level directory with the
`servoterm.pro` file. Qt6's qmake is typically a separate binary from Qt5's
(commonly `qmake6`); if `qmake` on your system still resolves to Qt5, use
`qmake6` explicitly:

```
qmake6
make
```

### CMake

Alternatively, using the included `CMakeLists.txt`:

```
cmake -B build
cmake --build build
```

## Running

If built with qmake, the `Servoterm` executable ends up in the same
directory as the `servoterm.pro` file:

```
./Servoterm
```

If built with CMake, it ends up in the build directory instead:

```
./build/Servoterm
```
