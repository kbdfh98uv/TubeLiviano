# TubeLiviano 0.5 · Win32 x86 portable

Cliente pequeño escrito en C para PCs antiguas: ventana Win32 Unicode, VLC 3
embebido, buscador y tres perfiles **144p · Mínimo**, **240p · Liviano**
(predeterminado) y **360p**. El destino del frontend es Windows XP SP3 de 32 bits
y Windows 10/11. **La compatibilidad y fluidez en un Atom N270 con 512 MB y XP
real todavía requieren pruebas en ese hardware.** Ver [validación](docs/VALIDACION.md).

## Extraer y usar

1. Extraé **todo** `TubeLiviano_Portable_0.5_x86.zip` a una carpeta corta, por
   ejemplo `E:\TubeLiviano`. No ejecutes el EXE dentro del ZIP. Podés usar pendrive;
   no necesita instalación, permisos de administrador, .NET ni navegador embebido.
2. Abrí `TubeLiviano.exe`. La versión **0.5** aparece en el título de la ventana.
3. Pulsá **Probar imagen y audio**: aparecen un patrón animado y un tono de 440 Hz.
   Funciona sin Internet. Seleccioná cada calidad para probar sus clips respectivos.
4. Escribí un título y pulsá **Buscar** o Enter. Cada resultado muestra título en
   la primera línea y canal + duración en la segunda. Doble clic o **Reproducir**
   abre el seleccionado. **Pegar / Reproducir** toma un enlace del portapapeles.
5. **Pausa** pausa/reanuda. **Cancelar** detiene VLC y termina yt-dlp y sus hijos.
   **Reintentar** conserva la búsqueda o el enlace. Cambiar de calidad vuelve a
   abrir el mismo enlace; no hace falta pegarlo otra vez.

El modo **Directo es experimental**: depende de los formatos y controles actuales
de YouTube. No hay fallback oculto a 720p, VP9 o AV1. Si el video no tiene H.264/AAC
dentro del límite elegido, probá 360p o el puente. Las URLs firmadas vencen;
Reintentar las vuelve a resolver. La aplicación no incluye anuncios ni recopila
telemetría propia; se conecta a YouTube/CDNs mediante las dependencias upstream.

## Puente opcional

En una PC más potente de la misma red ejecutá el servicio de
[`source/bridge/`](bridge/README.md) con Python, FFmpeg y yt-dlp actuales. En la
netbook elegí **Puente en red** y escribí, por ejemplo,
`http://192.168.1.20:8787`. Podés usar
`http://192.168.1.20:8787?token=clave-local` si el servidor se inició con `--token`.
Usá un token de letras, números, `-_.~`. El servidor transcodifica a H.264 baseline,
YUV420p, **24 fps**, AAC y transmite MPEG-TS en cuanto hay datos. La netbook no
transcodifica. El buscador sigue usando el motor local; también podés pegar el
enlace directamente en modo puente. Python/FFmpeg son requisitos de la PC puente,
**no de la netbook**. No expongas el puente a Internet.

## Carpetas y motores

```text
TubeLiviano.exe                 Frontend nativo x86, código propio MIT
vlc/                            DLLs y plugins nativos VLC 3.0.21 x86
engines/xp/yt-dlp.exe            Fork x86 publicado específicamente para XP
engines/modern/yt-dlp.exe        Build OFICIAL yt-dlp_x86.exe para Win10/11
runtimes/qjs.exe                 Motor JavaScript de desafíos (ver avisos)
media/prueba-{144,240,360}.mp4    Imagen animada y tono offline
source/                         Código propio, compilación, puente y pruebas
licenses/                       Licencias y hashes de terceros
```

Se consulta la versión real con `RtlGetVersion`, evitando las versiones ficticias
por falta de manifiesto. En Windows 10/11 **solo** se elige `engines/modern`; en XP
y sistemas anteriores a Windows 10 se usa `engines/xp`. Nunca se prueba el motor
XP como fallback en Windows moderno. Mantené las carpetas separadas. No se
descargan componentes al iniciar ni se usa el directorio de trabajo como raíz:
los recursos se ubican respecto al EXE.

El frontend no incorpora Python. Los builds upstream de yt-dlp **sí empaquetan su
propio intérprete**; esto es inherente a los ejecutables solicitados y pesa más que
la ventana Win32. Se carga un solo motor, durante la búsqueda/resolución, y se
libera al terminar o cancelar. No se garantiza el consumo total en 512 MB sin
medirlo en la máquina real.

## Streaming y latencia

El camino normal ejecuta yt-dlp con `--get-url`, elige H.264/AAC con altura máxima
144/240/360 y FPS hasta 30, y entrega las URLs a libVLC inmediatamente. Puede abrir
video y audio separados mediante `input-slave`, sin unirlos en disco ni ejecutar
FFmpeg en la netbook. VLC realiza las peticiones HTTP y Range al origen; **no hay
descarga completa previa**. Búfer de red: 450 ms; prefetch limitado a 128 KiB y
lecturas de 16 KiB, frente al prefetch predeterminado de VLC de 16 MiB.

Solo si falla la resolución inicial o VLC no abre el stream se repite una vez con
`--check-formats`. No se ejecuta `-F` antes de cada video. Tras el segundo fallo
queda visible Reintentar y el enlace se conserva. Las operaciones de yt-dlp tienen
tiempo límite y salida acotada en memoria. El puente usa MPEG-TS en vivo, sin
buscar por Range; el modo directo aprovecha Range del servidor original.

La meta de primer cuadro con ~14 % de un video de 96 s **es una medición a realizar
por origen/red/hardware, no una constante deducible del búfer**. La prueba local
reproducible y los resultados reales se documentan en `docs/VALIDACION.md`.

## Compilar desde Linux

Herramientas: MinGW-w64 x86 con CRT **msvcrt** (no UCRT para XP), `make`, Python 3
solo en el host de build, FFmpeg con libx264 para generar los clips.

```sh
# Debian/Ubuntu con permisos para instalar:
apt-get install gcc-mingw-w64-i686-win32 binutils-mingw-w64-i686 mingw-w64-i686-dev make ffmpeg
# Alternativa sin root: scripts/bootstrap-tools.sh /workspace/toolchain
make
python3 scripts/fetch-deps.py --sources
make clips
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 tests/engine_native_test.py
# Con Windows o Wine de 32 bits realmente funcional y servidor X:
WINE=wine python3 tests/playback.py
python3 scripts/package.py --slim-vlc
```

`make PREFIX=/ruta/i686-w64-mingw32-` permite otro toolchain. El EXE se enlaza con
DLLs del sistema XP, carga libVLC dinámicamente y no requiere un runtime GCC
adicional. El script de dependencias verifica TLS y hashes upstream fijados; no
desactiva comprobaciones ni descarga versiones flotantes. Para actualizar motores,
modificá versiones/sumas explícitamente y repetí las pruebas. No sobrescribas el
motor moderno con el de XP.

Para probar la app desde Windows:

```bat
TubeLiviano.exe --self-test-media media\prueba-240.mp4 --report prueba.json --test-ms 5500
```

El modo automatizado usa salida de audio dummy, comprueba decodificación de imagen
y audio y deja contadores JSON; para oír sonido usá el botón offline normal. Las
pruebas de reproducción automatizadas no equivalen a medir fluidez en el Atom.

## Licencias y tamaño

Código propio **MIT**, incluido en `source/`. VLC, yt-dlp, QuickJS y sus
dependencias mantienen sus licencias; ver [THIRD_PARTY.md](THIRD_PARTY.md). El
compañero `TubeLiviano_ThirdParty_Sources_0.5.zip` contiene fuentes upstream;
conservá sus avisos y requisitos al redistribuir.

Los dos motores suman aproximadamente **38 MB antes de VLC**. Con versiones
actuales completas y verificadas, la meta de ~40 MB para todo el ZIP no se cumple.
El tamaño y SHA-256 exactos quedan en `package-report.json`; no se sustituye un
motor por un descargador ni se presenta un paquete incompleto como portable.

YouTube puede bloquear solicitudes (403/429), exigir verificación o cambiar sus
desafíos. No se incluyen cookies, cuentas ni credenciales. Actualizar las
dependencias o resolver en el puente puede ser necesario. El soporte de XP de un
fork no garantiza acceso permanente a YouTube ni compatibilidad certificada en XP.
