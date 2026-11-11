# Validación de TubeLiviano 0.5

El entregable es un **frontend Win32 x86**, compilado desde Linux con MinGW y CRT
MSVCRT, dirigido a Windows XP SP3 y Windows 10/11. Se verificaron la arquitectura
PE32, el subsistema 5.01 y la ausencia de runtime .NET. Eso **no equivale a haberlo
ejecutado en XP**, ni certifica el paquete completo para ese sistema.

## Qué se ejecutó y dónde

El host de build no permite ejecutar IA32. Un programa ELF de 32 bits fue rechazado;
Wine 10 WoW64 falló al iniciar PE32, Wine 11 devolvió `c00000e5` y las alternativas
QEMU investigadas tampoco permitieron ejecutar el programa x86. No se cuentan esas
pruebas como aprobadas.

Para validar el comportamiento sin ocultar este límite se compiló una variante
**temporal x64 del mismo código C**, con las mismas opciones de aplicación, y se
ejecutó bajo Wine 10. Se usaron los DLLs x64 equivalentes de la misma distribución
oficial LibVLC 3.0.21, incluidos los mismos nombres de plugins del perfil reducido.
**Esa variante x64 no se incluye en el ZIP x86.** Las diferencias de arquitectura,
Windows, controladores y hardware siguen requiriendo prueba en el equipo destino.

Los resultados actuales proceden de `build/reports/` en el workspace de build.
El script elimina cada informe anterior antes de ejecutar la prueba y exige
terminación exitosa y contadores positivos de imagen y audio. No se considera
suficiente que VLC abra un puerto, que exista un proceso o que el reproductor diga
«reproduciendo».

| Comprobación | Resultado | Alcance |
| --- | --- | --- |
| Compilación del frontend x86 | Aprobada | PE32/i386, subsistema Windows 5.01, DLLs del sistema y libVLC cargado dinámicamente. |
| Selección de motores y subprocesos | 56 comprobaciones aprobadas | Código Win32 ejecutado en la variante x64/Wine: JSON, Unicode, argumentos, perfiles, fallback, ausencia del motor seleccionado y terminación del hijo y nieto al cancelar. |
| Parser y escapado | 38 comprobaciones aprobadas | Código de producción extraído por el harness nativo, con ASan y UBSan. No sustituye una ejecución de los subprocesos en XP. |
| Interfaz real | 32 comprobaciones aprobadas | Variante x64/Wine con extractor simulado: resultados Unicode, dos líneas, perfil predeterminado, enlace conservado al cambiar calidad/reintentar y cierre/cancelación de procesos descendientes. |
| Puente | 6 pruebas aprobadas | FFmpeg real, tres perfiles, salida temprana desde fuente de 96 s, exclusión mutua, desconexión, parámetros y runtime JS. |
| QuickJS-NG adaptado | Aprobado en variante x64 | JSON, Unicode, Uint8Array, BigInt, Promises y script UTF-8; el binario x86 recibió auditoría estática de imports, sin ejecución. |
| YouTube en vivo | No ejecutado | El proxy del entorno rechazó `CONNECT` a `www.youtube.com` con HTTP 403. No se obtuvo un video real ni un desafío EJS real. |
| XP SP3 / Windows 10 y 11 reales | Pendiente | No hubo máquinas Windows reales disponibles para esta validación. |
| Atom N270, 512 MB | Pendiente | Faltan mediciones de RAM, primer cuadro, cuadros perdidos, fluidez y sonido audible. |

## Clips offline: imagen y audio

Se generaron clips de ocho segundos con patrón animado y tono de 440 Hz: H.264
baseline, YUV420p, 24 fps, AAC mono, MP4 con `faststart`. Se reprodujeron con el
perfil reducido de plugins de VLC de la variante temporal x64.

| Perfil | Video decodificado | Audio decodificado | Cuadros mostrados | Búferes de audio reproducidos | Resultado |
| --- | ---: | ---: | ---: | ---: | --- |
| 144p | 134 | 238 | 44 | 119 | Aprobado |
| 240p | 182 | 346 | 63 | 173 | Aprobado |
| 360p | 170 | 320 | 66 | 160 | Aprobado |

Los informes se tomaron aproximadamente a los 5,5 s; no representan el total de
ocho segundos. El modo automatizado usa un dispositivo de audio dummy: comprueba
decodificación y entrega de audio, **no escucha física de los parlantes**. El botón
«Probar imagen y audio» normal usa la salida de audio de Windows. Los contadores
de Wine tampoco sirven para predecir la tasa de cuadros en un Atom.

## Streaming HTTP Range: fuente de 96 segundos

`tests/playback.py` sirvió un MP4 local de **4.428.555 bytes**, generado a 240p y
24 fps, desde un origen HTTP con Range y límite de 64 KiB/s. VLC realizó la petición
`Range: bytes=0-`; el servidor respondió con contenido parcial. La aplicación abrió
esa URL directamente, sin descargarla a un archivo temporal.

En la ejecución final del perfil reducido:

- Primer cuadro observado: **229.376 bytes leídos**, **5,18 %** del archivo, según
  el contador de entrada de libVLC en el primer sondeo con cuadros mostrados.
- A los 4,572 s: 122 cuadros de video decodificados, 234 bloques de audio
  decodificados, 20 cuadros mostrados y 117 búferes de audio reproducidos.
- Total enviado por el origen hasta detener la prueba: **284.672 bytes**, **6,43 %**
  del archivo. No se recibió el 100 % antes de reproducir.

Esto prueba salida temprana y uso de Range bajo esas condiciones. El sondeo tiene
granularidad de 100 ms y las estadísticas de VLC tienen su propia actualización;
el contador de lectura puede diferir de los bytes ya enviados por el socket.
El porcentaje no incluye cabeceras de red, no mide YouTube y **no garantiza ~14 %
en cualquier video, conexión o máquina**. El tamaño del MP4, su ubicación del índice,
el bitrate, los streams separados y el prefetch influyen en la cifra.

El búfer de red configurado es 450 ms. El prefetch se limita a 128 KiB, con lecturas
de 16 KiB. El puente transmite MPEG-TS en vivo y no admite búsqueda por Range;
el modo directo usa las capacidades Range de los servidores de los streams.

## Limitaciones que siguen abiertas

1. **Validación x86 real:** extraer el ZIP final en XP SP3, Windows 10 y Windows 11;
   probar el EXE y los motores incluidos, no los binarios temporales de validación.
2. **Atom real:** probar primero 240p, luego 144p y 360p. Medir la memoria del
   frontend, VLC y yt-dlp por separado y en conjunto. El intérprete Python de los
   ejecutables yt-dlp puede ser significativo con 512 MB aunque el frontend sea
   pequeño. No hay cifra de RAM del Atom medida en esta entrega.
3. **YouTube real:** comprobar búsqueda, enlaces, video/audio separados, cancelación
   durante la resolución y expiración de URLs. En este entorno se observó un 403
   del proxy; **no se observó un HTTP 429 de YouTube**. El servicio puede exigir
   verificación/cookies o cambiar sus desafíos en otros entornos.
4. **TLS en XP:** se incluyen raíces Mozilla actuales y se mantiene la validación
   de certificados. Sigue pendiente verificar una conexión real con la combinación
   XP, VLC 3 y el servidor remoto. No se desactivó TLS para superar fallos.
5. **Tamaño:** los dos motores suman aproximadamente 38 MB antes de añadir VLC.
   El ZIP reducido ronda 53 MB y supera la meta de ~40 MB. El tamaño y SHA-256
   definitivos se registran en el archivo externo `package-report.json`.
6. **Fuentes de terceros:** el compañero de fuentes incluye VLC, yt-dlp, QuickJS,
   EJS, Certifi y recetas. No contiene todos los tarballs transitivos de VLC/Python.
   Antes de distribución pública se debe completar esa provisión según sus
   licencias; véase `THIRD_PARTY.md`. La licencia MIT cubre el código propio.

## Repetir las comprobaciones

Desde la raíz del repositorio, o desde `source/` en el ZIP:

```sh
make
python3 scripts/fetch-deps.py --sources
make clips
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 tests/engine_native_test.py
# Solo con Wine x86 funcional; en Windows se puede usar el modo self-test directo.
WINE=wine python3 tests/playback.py
python3 scripts/package.py --slim-vlc
python3 -m unittest discover -s tests -p test_package.py -v
```

Las pruebas de ZIP se omiten explícitamente si aún no existe el paquete; deben
ejecutarse después de empaquetar. El script de reproducción falla si no se ejecuta
la aplicación o si faltan imagen/audio: no se sustituye por una prueba trivial.
La aplicación también permite comprobar un clip desde Windows:

```bat
TubeLiviano.exe --self-test-media media\prueba-240.mp4 --report prueba.json --test-ms 5500
```

Después, usar el botón offline para confirmar sonido audible y probar Cancelar,
Reintentar y cambio de perfil. Conservar el JSON, versión del OS, calidad elegida,
RAM máxima y errores observados para comparar con futuras versiones.
