# Guía de compilación de OVITO (fork ovito-ptm) en Linux

Probado en Ubuntu 24.04, GCC 13.3, CMake 3.28, Qt 6.4.2. Es la misma receta que usa el CI
(`.github/workflows/linux.yml`).

## 1. Dependencias del sistema

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake git ninja-build ccache \
  qt6-base-dev qt6-base-private-dev qt6-svg-dev \
  libgl-dev libegl-dev libxkbcommon-dev libvulkan-dev \
  libboost-dev libnetcdf-dev zlib1g-dev libzstd-dev
```

`ccache`, `libegl-dev` y `libzstd-dev` son opcionales: sin ellos el proyecto también configura y compila
(zstd se toma del submódulo `src/3rdparty/zstd`).

## 2. Código fuente y submódulos

```bash
cd ~/ovito-ptm
git submodule update --init --recursive   # descarga src/3rdparty/zstd
```

## 3. Configurar

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOVITO_BUILD_SSH_CLIENT=OFF
```

Con ccache instalado se puede acelerar la recompilación añadiendo:

```bash
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

## 4. Compilar

```bash
cmake --build build --parallel 8
```

Cada unidad de compilación puede usar más de 2 GB de RAM: con ~16 GB usar `--parallel 4`, con ~32 GB
`--parallel 8`. Si el compilador muere por falta de memoria (`killed signal terminated program cc1plus`),
bajar el número de jobs y relanzar el mismo comando (continúa donde quedó).

Para recompilar después de cambiar código basta con repetir el paso 4.

## 5. Ejecutar

```bash
./build/bin/ovito
```

## 6. Tests (opcional)

```bash
LIBGL_ALWAYS_SOFTWARE=1 ctest --test-dir build --output-on-failure --timeout 600
```

## 7. AppImage portable (opcional)

```bash
bash research/linux_build/build_appimage.sh build appimage
# resultado: appimage/OVITO-x86_64.AppImage
```

## Limpiar

```bash
rm -rf build      # borra todo y obliga a reconfigurar (paso 3)
```
