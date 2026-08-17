# KalaCLI

This is a cli executable template for C++ 20 on Windows and Linux. 

This project relies on several [external dependencies](https://github.com/greeenlaser/external-shared), they are not shipped inside this project, please make sure you have that repository cloned into a folder inside the same parent directory as this project folder before compiling this project from source.

## Projects that use this template

[KalaMake](https://github.com/kalakit/kalamake)

## Minimal compile example

To compile KalaCLI on Windows without Kalamake:
```
//create the root build dir
mkdir build

//create the target binary dir
mkdir build\release-windows

//create the object dir
mkdir build\release-windows\obj

//go to the target binary dir
cd build\release-windows

//compile cpp files
for /r "..\..\src" %f in (*.cpp) do clang++ -DLIB_STATIC -std=c++20 -c "%f" -I"..\..\include" -I"..\..\..\external-shared\KalaHeaders\include" -o "obj\%~nf.obj"

//link into library
llvm-lib /OUT:kalacli.lib "obj\*.obj"
```
And on Linux:
```
//create the root build dir
mkdir build

//create the target binary dir
mkdir build\release-linux

//create the object dir
mkdir build\release-linux\obj

//go to the target binary dir
cd build\release-linux

//compile cpp files
for f in ../../src/**/*.cpp
    clang++ -DLIB_STATIC -std=c++20 -c "$f" -I"../../include" -I"../../../external-shared/KalaHeaders/include" -o obj/(basename $f .cpp).o
end

//link into library
ar rcs libkalacli.a obj/*.o
```

---

## Links

[Donate on PayPal](https://www.paypal.com/donate/?hosted_button_id=QWG8SAYX5TTP6)

[Official Discord server](https://discord.gg/BZ2eJ5Cdr8)

[Official Youtube channel](https://youtube.com/greenlaser)

---

## Docs

[How to build from source](docs/build_from_source.md)

[External libraries](docs/external_libraries.md)

[Lost Empire Entertainment and KalaKit ecosystem](docs/ecosystem.md)
