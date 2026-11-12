# Validación de TubeLiviano

La versión actual 0.5.4 documenta latencia, controles y límites en
[RELEASE_NOTES_0.5.4.md](RELEASE_NOTES_0.5.4.md), con evidencia en
`evidence/0.5.4/`. Para medir el equipo real, usar
[PRUEBAS_MANUALES_0.5.4.md](PRUEBAS_MANUALES_0.5.4.md).
Los resultados siguientes se conservan como registro de **0.5.2**, no como
ejecución del binario actual.

El entregable es Win32 x86/MSVCRT, con OS y subsistema PE 5.01. El EXE y todos
los DLL/EXE del ZIP se inspeccionan como PE32/i386; libVLC se carga dinámicamente.
**[NO EJECUTADO] Windows XP, Windows 10/11 y Atom físicos.** Este entorno no puede
ejecutar IA32. Las comprobaciones de comportamiento se hacen con una variante
**x64 del mismo código**, Wine 10 y DLLs x64 equivalentes de LibVLC 3.0.21. Esa
variante no entra en el ZIP x86 ni certifica el equipo de destino.

## Evidencia conservada

- `evidence/0.5.2/clean-build.json`: dos builds limpios de la base 0.5.1, idénticos
  al EXE publicado de 54.784 bytes.
- `evidence/0.5.2/compatibility-audit.json`: manifiesto, imports y motores fijados;
  [auditoría A1–A9](AUDITORIA-0.5.2.md) y regresiones antes/después.
- `evidence/0.5.2/dependency-repro.json`: QuickJS reconstruido dos veces, timestamp
  cero y SHA-256 `22539f9cd1253230f98f9629cad610863149acafd72f1d52d162fb57c56a3fd2`.
- `evidence/0.5.2/release-build.json`: build final limpio, entradas y comandos exactos.
- `evidence/0.5.2/release-validation.json`: reproducción y controles de la candidata
  final; identifica arquitectura, hash del EXE de prueba, comandos y resultados.
- `evidence/0.5.2/ui-captures.json`: capturas de la interfaz nativa en Wine x64,
  con motores reales presentes pero sin iniciar búsqueda ni reproducción.

Los informes baseline y `offline-final.json` corresponden al hito de estabilidad
**anterior al rediseño**. No se presentan como ejecución de la UI final. Los
informes de medios reales se distinguen de los tests con extractor/VLC simulados.

## Qué comprueban las pruebas

El runner `scripts/verify-offline.py` construye desde las fuentes actuales una
etapa nueva y comprueba hashes upstream, arquitectura real y perfiles VLC antes
de arrancar. Borra informes viejos y exige salida cero, `complete`, `passed` y
contadores positivos de video decodificado, audio decodificado, cuadros mostrados
y búferes de audio. Un fallo tardío o un informe no escribible hacen fallar el
self-test. Las regresiones cubren esos casos con un VLC simulado explícito.

Los clips 144/240/360p duran ocho segundos: H.264 constrained baseline, YUV420p,
24 fps, AAC mono 44,1 kHz y MP4 con índice antes de los datos. FFprobe verifica
sus perfiles y la reproducción usa plugins del mismo conjunto reducido que el
portable. Audio dummy comprueba decodificación y entrega; **[NO EJECUTADO] sonido
audible físico**. El botón offline normal usa el dispositivo de Windows.

El origen Range sirve un MP4 de 96 s y 4.428.555 bytes a 64 KiB/s. Se exige una
petición Range, primer cuadro con lectura parcial y que el origen no entregue el
archivo entero. Se conserva el contador leído al primer sondeo con cuadros; no
se afirma que coincida byte a byte con lo enviado por el socket. No es YouTube.

La ventana de observación de este test se amplió de 4,5 a 12 s: se conservó una
ejecución fallida con solo 73.728 bytes recibidos y cero cuadros, cuando el plazo
incluía inicialización fría de VLC/plugins. No se quitaron las condiciones de
imagen/audio ni de descarga parcial. Véase `range-short-window-failure.json`.
El plazo permite arranque frío y no certifica una latencia máxima de 12 s.

Los controles Win32 prueban selección del OS, subprocesos y descendientes,
Unicode/NUL/JSON, recuperación, rutas largas, cierre asíncrono y DLL privada de
VLC. Configuración prueba todos los campos, límites, INI Unicode, guardado atómico,
solo lectura y tokens solo de sesión. La UI prueba navegación, teclado, cambio
de tema/letra, cancelar/guardar, reapertura y geometría de cliente 660×450 con
texto grande y puente. No descarga miniaturas ni usa animaciones.

**[NO EJECUTADO] Comparación de recursos GDI/USER:** Wine devuelve cero en
`GetGuiResources`; el test lo declara y no transforma esos ceros en prueba de
que no hay fugas. El checklist físico permite registrar memoria/handles reales.

## Resultado final de la candidata

EXE entregable x86: **84,992 bytes**, SHA-256
`e02736d7087d3c8f228b1cf930b3214f5ee6fc8dfd3089e67858eeff2a8ec440`. Tres builds (raíz y dos carpetas nuevas sin outputs)
resultaron idénticos. Solo importa GDI32, KERNEL32, MSVCRT, SHELL32 y USER32;
PE32/i386, OS/subsistema 5.01, sin CLR y sin timestamps del linker.

El runner final aprobó **953 comprobaciones** con la variante x64/Wine:

| Suite | Comprobaciones / fallos |
| --- | ---: |
| os | 23 / 0 |
| engine | 115 / 0 |
| gui | 32 / 0 |
| main | 35 / 0 |
| loader | 19 / 0 |
| settings | 180 / 0 |
| player-options | 302 / 0 |
| ui-settings | 120 / 0 |
| batch | 127 / 0 |

Los tests `main`, `player-options` y `batch` usan medios/contadores simulados
explícitos. El loader reproduce medios reales con una DLL core señuelo en raíz.
El motor usa un extractor fixture para condiciones reproducibles, sin YouTube.
El batch comprueba salidas 0, `0xC0000005` y `0xFFFFFFFF`, aun con JSON aprobado.
La dirección del puente se conserva sin exigir pérdida de foco y el token no
vuelve al reiniciar. Las regresiones previas fallaban en esos casos.

Clips reales reproducidos (audio dummy):

| Perfil | Video decodificado | Audio decodificado | Cuadros mostrados | Búferes de audio |
| --- | ---: | ---: | ---: | ---: |
| 144p | 206 | 370 | 86 | 185 |
| 240p | 218 | 389 | 90 | 195 |
| 360p | 230 | 432 | 95 | 216 |

Range real: petición `bytes=0-`; primer cuadro con **157,696 / 4,428,555
bytes leídos (3.56%)**, antes de recibir el archivo completo.
El origen envió 757,760 bytes durante la observación. El porcentaje es
resultado de esta fuente/origen/variante, no una garantía en YouTube o Atom.

Los tests nativos de parser (38) y política de OS (21) también pasaron con
ASan/UBSan. Las comprobaciones del ZIP y sus hashes se registran tras empaquetar
en el [cierre de publicación](https://github.com/kbdfh98uv/TubeLiviano/blob/main/releases/CHECKPOINT_0.5.2.md),
fuera del propio ZIP para evitar una referencia circular al hash.


## Repetir sin hardware

Desde la raíz del repo o `source/` del ZIP, con MinGW i686/MSVCRT y Python 3.12+:

```sh
make -B
python3 scripts/fetch-deps.py --offline --sources  # caché completa previamente
make clips
python3 tests/engine_native_test.py
cc -std=c99 -fsanitize=address,undefined tests/os_detection_test.c src/os_detect.c -o build/os_detection_native
build/os_detection_native
python3 -m unittest discover -s tests -p 'test_*.py' -v
# Requiere Wine x86 funcional y DISPLAY. --help describe rutas/servidor X opcionales.
python3 scripts/verify-offline.py --control-tests
python3 scripts/package.py --slim-vlc
python3 -m unittest discover -s tests -p test_package.py -v
```

Este entorno ejecutó la variante explícita mediante:

```sh
WINEDLLOVERRIDES='mscoree,mshtml=' python3 scripts/verify-offline.py \
  --surrogate-x64 --control-tests \
  --cc-prefix /workspace/toolchain/root/usr/bin/x86_64-w64-mingw32- \
  --wine /workspace/toolchain/wine-10.0-amd64-wow64/bin/wine \
  --wine-prefix /workspace/toolchain/wineprefix10 \
  --xvfb /workspace/toolchain/root/usr/bin/Xvfb \
  --report-dir build/reports/session-0.5.2/publish-verified
```

La opción `--surrogate-x64` nunca se interpreta como una prueba del EXE x86.
Las pruebas del ZIP se omiten explícitamente antes de empaquetar y se ejecutan
contra el archivo final antes de publicarlo. En Windows, `Probar_offline.cmd`
funciona sin Python/PowerShell y conserva JSON nuevos por perfil en `%TEMP%`.

## Límites abiertos

1. **XP/framework:** los dos motores conservan sus versiones y hashes upstream.
   Elegir XP correctamente no prueba su framework: los errores reportados de
   `KERNELxp.dll`/`ntext.dll` requieren reproducirse en la máquina XP. Probar
   `engines\xp\yt-dlp.exe --version` por separado antes de atribuir un fallo a
   YouTube; no se sustituyen DLLs del sistema.
2. **Atom 512 MB:** faltan RAM, CPU, fluidez, sonido y primer cuadro medidos. El
   tamaño del frontend no equivale al de VLC ni los intérpretes empaquetados por
   yt-dlp. El perfil recomendado sigue siendo 240p, con 144p como mínimo.
3. **YouTube/TLS reales:** el proxy del build bloqueó CONNECT al servicio con
   HTTP 403; no se observó un HTTP 429 de YouTube. Verificación/desafíos o 429
   pueden aparecer fuera de aquí. No se desactivan certificados para superarlos.
4. **Tamaño/fuentes:** el ZIP supera la meta de ~40 MB; los dos motores ya pesan
   unos 38 MB. Las fuentes transitivas pendientes se describen en
   [THIRD_PARTY.md](../THIRD_PARTY.md). MIT cubre el código propio.

La siguiente prueba útil está en [PRUEBAS_XP_ATOM.md](PRUEBAS_XP_ATOM.md): cargar
frontend, reproducir tres clips, probar motor XP y recién después medir YouTube.
