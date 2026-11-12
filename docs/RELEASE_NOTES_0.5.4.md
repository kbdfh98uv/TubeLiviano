# TubeLiviano 0.5.4 · preparación y reapertura más rápidas

Release sobre 0.5.3. El objetivo es reducir la espera hasta la imagen
sin aumentar la calidad predeterminada ni perder Win32 x86/XP como destino.
Se conserva VLC 3: todavía no hay una comparación que demuestre que otro
reproductor reduzca mejor el tiempo total de búsqueda, resolución y primer cuadro.

## Cambios de arranque

- Al pedir reproducir, la carga de DLLs y la preparación de VLC pasan a su hilo
  de control mientras yt-dlp resuelve el enlace. La ventana puede atender
  Cancelar durante esa preparación. Buscar un título por sí solo no prepara VLC
  ni inicia solicitudes de video especulativas.
- Si falta `vlc/plugins/plugins.dat`, VLC intenta generar un índice nativo de
  plugins. Las siguientes inicializaciones pueden leer sus descripciones sin
  cargar todos los DLLs para descubrirlas. El índice guarda rutas relativas y
  conserva la validación normal de versión, tamaño y fecha de los plugins.
- El índice no es un video descargado. Un fallo de escritura en una carpeta
  protegida no debe impedir reproducir; se conserva el escaneo normal como
  alternativa. No se distribuye un índice x64 de las pruebas como si fuera x86
  y no se desactiva el descubrimiento con `--no-plugins-scan`.
- Se mantienen como inicio 240p, un hilo, búfer de red 450 ms y lectura anticipada
  128 KiB. El preset Mínimo sigue disponible para hardware/conexiones que lo
  necesiten; reducir el búfer no se presenta como una solución universal.

## Reabrir un enlace reciente

Una caché solo en memoria conserva hasta **dos combinaciones de enlace/calidad**
con vida máxima de **dos minutos desde su resolución**. Se guarda una entrada
solo cuando la apertura nueva produjo imagen y audio. No guarda el archivo de
video ni escribe las URLs de streams en el INI o en un historial de disco.

Volver a abrir exactamente ese enlace/calidad puede omitir otra resolución.
Usarlo no renueva su vencimiento. La primera apertura de otra calidad puede
necesitar yt-dlp; volver a una calidad ya probada puede reutilizar su entrada si
sigue vigente y no fue reemplazada por otra.

**Reintentar fuerza una resolución nueva** y evita esta caché. Un enlace guardado
que da error, o no muestra imagen durante unos seis segundos después de
preparar VLC, se descarta y se vuelve a resolver. Esto no garantiza que una URL
remota siga válida ni elimina los errores del servicio. Cerrar la aplicación
olvida esta caché de streams; el índice de plugins es un mecanismo separado.

## Medidas realizadas y límites de la mejora

Son pruebas con el mismo código compilado **x64 bajo Wine**, 69 plugins del perfil
reducido, medios generados y audio dummy. Cada proceso se abrió de nuevo, pero la
caché de archivos del sistema operativo estaba caliente. No representan un
arranque físico frío, una extracción real de YouTube ni un Atom N270.

| Prueba | Antes | Candidata | Alcance |
| --- | ---: | ---: | --- |
| Preparación local de VLC | 287 ms | 34 ms | Medianas de 3 muestras; candidata con índice ya creado |
| Primera creación del índice | — | 305 ms | Una muestra; no evita el trabajo inicial de descubrir plugins |
| Primer cuadro local | 1007 ms | 1131 ms | Medianas de 3; esa serie no mostró mejora total de imagen |
| Primer cuadro HTTP, búfer normal | 2099 ms | 2182 ms | Medianas de 3; el índice por sí solo no aceleró esta serie |
| HTTP + resolución simulada de 900 ms, serie A | 3015 ms | 2912,5 ms | 4 muestras por grupo; índice + preparación solapada |
| HTTP + resolución simulada de 900 ms, serie B | 2945 ms | 2753 ms | 3 muestras por grupo; índice + preparación solapada |
| Repetición con script reproducible | 3930,5 ms | 3757 ms | 4 muestras por grupo intercaladas; resolver simulado de 900 ms |

La mejora de preparación ronda 250–270 ms cuando el índice existe; **no equivale
a ese mismo ahorro en el primer cuadro**. En las series con resolver simulado,
el ahorro observado de primer cuadro fue aproximadamente 100–200 ms. El resultado
puede cambiar con el disco, antivirus, carga del sistema, driver, red y servidor.
No se promete ese ahorro en YouTube ni en el Atom real.

La repetición actual se conserva en `evidence/0.5.4/startup-comparison.json`.
El script `scripts/benchmark-startup.py --help` explica cómo repetirla, y
`tests/plugin_index_probe.py` prueba índice existente/ausente en una carpeta que
realmente deniega escritura, además de una ruta movida con Unicode. Las tres
pruebas reprodujeron imagen/audio con VLC real bajo Wine; no son ACLs de XP.

Los registros de desarrollo están en el workspace:
`build/latency-054/measurements/summary.json`,
`build/latency-054/preload-measurements/summary.json` y
`build/latency-054/warmclip-measurements/summary.json`.

Se descartaron la precarga adicional de DLLs y reproducir un clip oculto para
calentar el reproductor: no mejoraron el primer cuadro frente a la candidata
simple en esas pruebas. La última serie dio 3033 ms con clip oculto frente a
2753 ms con índice/preparación solapada. No se mantienen tareas de fondo por una
ventaja que no quedó demostrada.

## Reutilización breve del motor de YouTube

El adaptador externo en `plugins/` usa la API pública de yt-dlp. La aplicación
conserva su intérprete como máximo **12 segundos tras Buscar**, permitiendo
resolver el resultado seleccionado sin arrancar otro. Al resolver, entrega las
URLs a VLC antes de esperar la limpieza del motor y luego lo cierra. No se
modifican ni reempaquetan los ejecutables XP/moderno. Cancelar/Inicio/cerrar,
Puente, el clip offline y una reapertura desde caché liberan el motor. En resultados,
Cancelar sigue disponible mientras exista ese proceso conservado.

Un Job Object posee su árbol de procesos, incluidos QuickJS y el intérprete
empaquetado. El canal usa UTF-8 con límites, timeouts y respuestas enmarcadas; un
motor que deja de leer stdin también puede cancelarse. Los fallos DLL/crash,
timeouts y protocolo inválido no lanzan otro extractor automáticamente. Si falta
el adaptador o el motor sale limpiamente sin admitirlo, se conserva el CLI
tradicional. La elección de motor por OS sigue siendo la misma.

La selección de calidad se recompila al cambiar el perfil dentro del mismo
intérprete: cambiar solo el campo `format` de yt-dlp habría conservado su selector
HD inicial. Los tests incluyen formatos 720p/1080p para comprobar que se siguen
eligiendo exclusivamente AVC/AAC 144/240/360p y que el probing actúa solo en fallback.

Comparación de tres muestras con el actor C y plugin productivos, build oficial
**x64** fijado y un extractor fixture offline bajo Wine:

| Fase | Mediana |
| --- | ---: |
| Buscar en un motor nuevo | 3684 ms |
| Resolver reutilizando ese mismo PID | 38 ms |
| Resolver en un motor nuevo, misma URL/perfil fixture | 3444 ms |
| Terminación posterior a la respuesta, asíncrona | 446 ms |

Registro: `evidence/0.5.4/worker-official-comparison.json`. Estos números muestran
el costo de arrancar y limpiar el proceso, **no una resolución de YouTube de 38 ms**.
No incluyen tráfico/desafíos reales de YouTube ni ejecución x86/XP/Atom. La primera
apertura pegando un enlace todavía debe arrancar el motor; no se elimina ese costo.

Conservar Python brevemente consume RAM. El prototipo x64/Wine observó unos
84 MiB de working set del intérprete; no es una medida x86 ni memoria privada en
XP. No se deja retenido durante el video y falta medir paginación en 512 MB. No
se añaden precargas especulativas de videos ni un framework adicional.

## Validación final y entrega

El frontend C x86 usa LTO y omite sus descripciones GNU de unwind, manteniendo
SEH x86 y las DLLs upstream. Esto reduce información disponible para backtraces
GNU, sin comprimir el EXE ni agregar un cargador. Se comparten el escape de
argumentos y el entorno independiente entre ambos modos del motor.
[SUPUESTO] El presupuesto del frontend se expresa como **menos de 100 KiB**;
la prueba de tamaño usa esa unidad en lugar del anterior umbral decimal de
100000 bytes. Las opciones `-fdata-sections` y las precargas descartadas no entran
en el producto. Los comandos, imports y comparaciones están en la evidencia de build.

Frontend entregable: **101376 bytes (99 KiB)**, PE32/i386, OS/subsistema 5.01,
sin CLR y con las cinco DLLs del sistema previstas. Tres builds limpios, incluidos
dos directorios nuevos, produjeron el mismo SHA-256:
`39470649fa0316c687f5fef8eb60b08ca6a5f2128fd6651d5be5309d9414abae`.
Comandos e imports en `evidence/0.5.4/reproducible-build.json` y
`compatibility-audit.json`.

La validación integrada del mismo código **x64/Wine** aprobó 1692 comprobaciones:

| Suite | Comprobaciones / fallos |
| --- | ---: |
| os | 23 / 0 |
| engine | 115 / 0 |
| engine-worker | 100 / 0 |
| gui | 35 / 0 |
| main | 37 / 0 |
| loader | 19 / 0 |
| settings | 300 / 0 |
| player-options | 396 / 0 |
| ui-settings | 169 / 0 |
| fullscreen | 118 / 0 |
| fullscreen-media | 51 / 0 |
| batch | 127 / 0 |
| stream-cache | 106 / 0 |
| latency-gui | 96 / 0 |

Los clips reales 144/240/360p pasaron con video, audio y cuadros positivos.
El origen Range de 96 s mostró cuadros con **174080 de 4428555 bytes leídos
(3,93 %)**; no descargó el archivo completo. Se mantuvieron las comprobaciones
de pantalla completa con imagen visible antes/durante/después. Los tests de
controlador, actor y latencia usan fixtures explícitos; no se cuentan como
reproducción multimedia real.

El plugin pasó otras 70 comprobaciones con las fuentes fijadas de ambos motores
en Python del host; parsing/escape pasó 38 con ASan/UBSan. Eso no ejecuta el
framework XP. Los tests del ZIP y sus hashes definitivos se registran tras
empaquetar en el [cierre de publicación](https://github.com/kbdfh98uv/TubeLiviano/blob/main/releases/CHECKPOINT_0.5.4.md),
fuera del ZIP para evitar una referencia circular al checksum.

**[NO EJECUTADO] XP SP3, Atom N270/512 MB y Windows físicos:** faltan tiempo real
hasta imagen/sonido, RAM máxima, fluidez y comportamiento del índice en ese disco.
El framework XP y sus avisos KERNELxp/ntext requieren prueba en su máquina; una
mejora de latencia no demuestra que ese framework esté reparado. YouTube real no
se probó en estas medidas controladas; 403/429/verificación deben registrarse
por separado. Licencias y fuentes transitivas: [THIRD_PARTY.md](../THIRD_PARTY.md).

Entrega: `TubeLiviano_Portable_0.5.4_x86.zip`, compañero de fuentes y
sumas bajo `releases/`. Los ZIP anteriores se conservan. La guía para obtener
resultados físicos comparables es [PRUEBAS_MANUALES_0.5.4.md](PRUEBAS_MANUALES_0.5.4.md).
