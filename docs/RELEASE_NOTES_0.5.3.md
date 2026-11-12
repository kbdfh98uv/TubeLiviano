# TubeLiviano 0.5.3 · pantalla completa y presets

Cambios sobre 0.5.2. El entregable continúa siendo Win32 x86 portable, dirigido a
XP SP3/Atom y Windows 10/11; los motores, VLC y sus versiones fijadas se conservan.

## Pantalla completa

- Botón visible en Resultados, junto a Configuración; se habilita al reproducir.
  También funciona con el clip offline. F11 alterna entre la ventana y el monitor
  actual, manteniendo el mismo HWND de video y reproductor sin resolver ni abrir
  de nuevo el stream.
- La ventana ocupa el monitor sin bordes y conserva una barra inferior de 48 px
  con Pausa, Cancelar y Volver a ventana. Esc/F11 permiten volver; Esc conserva
  la reproducción y otra pulsación en la ventana normal cancela. El autorepeat
  de Esc no convierte la salida de pantalla completa en Cancelar.
- Cancelar, fin de reproducción o error restauran la vista normal. Se recuperan
  el tamaño/posición o estado maximizado anterior; al cerrar en pantalla completa
  se guarda el tamaño normal, evitando abrir luego una ventana del tamaño del monitor.
- Los atajos de reproducción se registran mientras esta aplicación está activa
  para atender el foco del hijo de VLC y se liberan al activar otra aplicación.
  Ctrl+F o Configuración permiten regresar a los controles de la ventana.

## Presets de configuración

General incorpora un selector con cinco presets y el botón **Preparar**. Cambia
solo el borrador; **Guardar** aplica/persiste y **Cancelar** lo descarta.

| Preset | Calidad | Búfer | Prefetch | Hilos | Modo |
| --- | --- | --- | --- | --- | --- |
| Mínimo | 144p | 250 ms | 64 KiB | 1 | Directo |
| Atom · recomendado | 240p | 450 ms | 128 KiB | 1 | Directo |
| Conexión inestable | 240p | 1500 ms | 256 KiB | 1 | Directo |
| 360p · Dos hilos | 360p | 600 ms | 128 KiB | 2 | Directo |
| Puente | 240p | 800 ms | 128 KiB | 1 | Puente |

Conservan volumen/silencio, tema/letra, resultados, timeouts, dimensiones y la
URL completa del puente en memoria. El token sigue sin escribirse en el INI.
Elegir un preset directo cambia el modo a Directo; el de Puente requiere el
servidor de otra PC. El volumen inválido se informa antes de preparar y no se
sustituye silenciosamente por otro valor. Se conserva 240p/un hilo como inicio.

## Validación y alcance

Frontend x86: **89,088 bytes**, PE32/i386, OS/subsistema 5.01,
solo las cinco DLLs del sistema previstas. Tres builds fueron idénticos: SHA-256
`8c6e533fac019b13800e4fc65820b3a2ea93f51ad36aa471b6c69625304247f2`.

Resultados actuales de la variante x64/Wine (no del EXE x86):

| Suite | Checks / fallos |
| --- | ---: |
| Settings y cinco presets | 300 / 0 |
| UI, borrador, Guardar/Cancelar y persistencia | 169 / 0 |
| Pantalla completa y restauración | 118 / 0 |
| VLC real: imagen visible antes/durante/después de fullscreen | 51 / 0 |
| OS, motor, GUI base, main, loader, controller y batch | 658 / 0 |
| Total sin duplicar suites de capturas | 1296 / 0 |

Los clips reales 144/240/360p y Range pasaron. Primer cuadro Range con
131,072 de 4,428,555 bytes (2.96%).
En fullscreen real hubo 177 cuadros mostrados y
345 búferes de audio dummy. Las capturas muestran el patrón real.

La validación combinó las suites aprobadas del runner con las repeticiones
puntuales del fixture de fullscreen, siempre sobre el mismo SHA-256 de app.
El fixture pasó a esperar la leyenda final del botón y un placement estable,
en lugar de leer estilos mientras aún se restauraban; no se quitaron las
aserciones originales de rectángulo, estado maximizado o continuidad de pausa.
Se aislaron carpetas TEMP de los tests para evitar INI heredados si Wine repite
PIDs. El comportamiento pre-maximizado de la ventana de Wine queda registrado
como límite del escritorio de prueba, no como ejecución de un XP físico.

Evidencia: `evidence/0.5.3/final-validation.json`, `fullscreen-handoff.json` y
`reproducible-build.json`. Los datos definitivos del ZIP y su publicación quedan
fuera del ZIP, en el [cierre](https://github.com/kbdfh98uv/TubeLiviano/blob/main/releases/CHECKPOINT_0.5.3.md),
para evitar una referencia circular al checksum.

**[NO EJECUTADO] XP SP3 y Atom físicos:** faltan carga, consumo, fluidez, salida
de video en pantalla completa y sonido en ese hardware. La variante x64/Wine
sirve para comprobar el flujo de controles; no certifica el EXE x86 ni los drivers
XP. Los presets son configuraciones propuestas, no resultados medidos en Atom.

El framework XP conserva sus binarios upstream; los avisos KERNELxp/ntext siguen
necesitando prueba allí. El servicio YouTube puede bloquear o pedir verificación;
las pruebas offline no prueban el servicio. El tamaño completo sigue dependiendo
de VLC y ambos motores, por encima de la meta original de ~40 MB. Licencias y
fuentes transitivas pendientes: [THIRD_PARTY.md](../THIRD_PARTY.md).

## Archivos y pruebas físicas

`TubeLiviano_Portable_0.5.3_x86.zip` y
`TubeLiviano_ThirdParty_Sources_0.5.3.zip`, con hashes en
`releases/SHA256SUMS.txt`. Los ZIP previos se conservan.

Extraer completo en una carpeta nueva, confirmar 0.5.3 y seguir
[PRUEBAS_0.5.3.md](PRUEBAS_0.5.3.md). Primero probar los tres clips offline;
después usar el motor XP y una fuente online accesible.
