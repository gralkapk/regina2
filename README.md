# REGINA2

REGisters and INstructions Analyzer (version 2) is an instrumenting profiler.

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

## Citing

**Visual Exploration of Memory Traces and Call Stacks**  
P. Gralka, C. Schulz, G. Reina, D. Weiskopf, T. Ertl  
Software Visualization (VISSOFT), 2017, p. 54 - 63  

```TeX
@inproceedings {Gralka2017Visual,
    author = {Gralka, Patrick and Schulz, Christoph and Reina, Guido and Weiskopf, Daniel and Ertl, Thomas},
    title = {Visual Exploration of Memory Traces and Call Stacks},
    year = {2017},
    booktitle = {Software Visualization (VISSOFT)},
    pages  = {54-63}
}
```

## License

Licensed under a BSD license, just like [DynamoRIO](http://www.dynamorio.org/).
