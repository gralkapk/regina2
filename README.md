# REGINA

REGisters and INstructions Analyzer is an instrumenting profiler.

## Installation

Clone the repository:

```
git clone git://github.com/UniStuttgart-VISUS/regina.git
```

Build on Windows using:

```
"C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86_amd64
mkdir build && cd build
cmake -G "Visual Studio 12 Win64" ..
cmake --build . --config RelWithDebInfo
```

## Usage

Run on Windows using:

```
drrun.exe -c regina.dll -- notepad.exe
```

## License

Licensed under a BSD license, just like [DynamoRIO](www.dynamorio.org).
